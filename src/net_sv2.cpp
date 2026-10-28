#include "net_sv2.h"
#include "core/sv2_noise.h"
#include "core/sv2_protocol.h"
#include "log.h"
#include "miner.h"
#include "net.h"
#include "settings.h"
#include "tls.h"
#include <esp_system.h>
#include <algorithm>
#include <ctime>
#include <memory>

namespace sv2 {
namespace {
bool writeAll(WiFiClient &client, const uint8_t *data, size_t length) {
    uint32_t start = millis(); size_t sent = 0;
    while (sent < length && client.connected() && millis() - start < 10000) {
        size_t n = client.write(data + sent, length - sent);
        if (!n) return false;
        sent += n;
    }
    return sent == length;
}
bool readAll(WiFiClient &client, uint8_t *data, size_t length) {
    uint32_t start = millis(); size_t got = 0;
    while (got < length && client.connected() && WiFi.isConnected() && millis() - start < 10000) {
        if (!client.available()) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
        int n = client.read(data + got, length - got);
        if (n < 0) return false;
        if (n) got += n; else vTaskDelay(pdMS_TO_TICKS(1));
    }
    return got == length;
}
// Incremental frame reception keeps share submission and pool acknowledgments
// moving even when a peer splits a frame over many TCP packets.
class Transport {
public:
    Noise noise;
    uint32_t lastRx = millis();
    bool send(WiFiClient &client, const Frame &frame) {
        if (frame.payload.empty() || frame.payload.size() > kMaxPayload) return false;
        uint8_t header[6] = {uint8_t(frame.extension), uint8_t(frame.extension >> 8), frame.type,
                            uint8_t(frame.payload.size()), uint8_t(frame.payload.size() >> 8), 0};
        uint8_t encrypted[22];
        if (!noise.encrypt(header, 6, encrypted) || !writeAll(client, encrypted, 22)) return false;
        std::vector<uint8_t> payload(frame.payload.size() + 16);
        return noise.encrypt(frame.payload.data(), frame.payload.size(), payload.data()) &&
               writeAll(client, payload.data(), payload.size());
    }
    int poll(WiFiClient &client, Frame &out) {
        if (started_ && millis() - frameStart_ > 30000) return -1;
        if (!client.available()) return 0;
        if (!started_) { started_ = true; frameStart_ = millis(); }
        int n = client.read(buffer_.data() + got_, buffer_.size() - got_);
        if (n < 0) return -1;
        if (!n) return 0;
        lastRx = millis(); got_ += n;
        if (got_ != buffer_.size()) return 0;
        if (!payload_) {
            uint8_t header[6];
            if (!noise.decrypt(buffer_.data(), 22, header)) return -1;
            extension_ = uint16_t(header[0]) | uint16_t(header[1]) << 8; type_ = header[2];
            size_t length = uint32_t(header[3]) | uint32_t(header[4]) << 8 | uint32_t(header[5]) << 16;
            if (length > kMaxPayload || (extension_ & 0x7fff)) return -1;
            got_ = 0;
            if (!length) { out = {extension_, type_, {}}; reset(); return 1; }
            buffer_.resize(length + 16); payload_ = true; return 0;
        }
        if (!noise.decrypt(buffer_.data(), buffer_.size(), buffer_.data())) return -1;
        buffer_.resize(buffer_.size() - 16);
        out.extension = extension_; out.type = type_; out.payload = std::move(buffer_);
        reset(); return 1;
    }
private:
    std::vector<uint8_t> buffer_ = std::vector<uint8_t>(22);
    size_t got_ = 0; bool payload_ = false, started_ = false; uint32_t frameStart_ = 0;
    uint16_t extension_ = 0; uint8_t type_ = 0;
    void reset() { buffer_.assign(22, 0); got_ = 0; payload_ = started_ = false; }
};
}

SessionResult session(WiFiClient &client, uint32_t maxMs, const char *identity,
                      void (*status)(const char *), std::atomic<uint8_t> &pending) {
    SessionResult result;
    const String &configured = netOnBackup() ? settings.backupAuthority : settings.poolAuthority;
    uint8_t authority[32];
    if (!authorityKey(configured.c_str(), authority)) { status("SV2 authority key missing/invalid"); return result; }
    time_t now = time(nullptr);
    if (now < 1700000000) { status("SV2 waiting for synchronized time"); return result; }
    auto transport = std::unique_ptr<Transport>(new (std::nothrow) Transport);
    auto protocol = std::unique_ptr<Protocol>(new (std::nothrow) Protocol);
    if (!transport || !protocol) { status("SV2 allocation failed"); return result; }
    uint8_t hello[64], response[234];
    status("SV2 encrypted handshake");
    core0WdtPause();
    bool begun = transport->noise.start(authority, esp_fill_random, hello);
    core0WdtResume();
    if (!begun || !writeAll(client, hello, sizeof(hello)) || !readAll(client, response, sizeof(response))) {
        status("SV2 Noise handshake failed"); logf("[pool] SV2: %s\n", transport->noise.error()); return result;
    }
    core0WdtPause();
    bool verified = transport->noise.finish(response, static_cast<uint32_t>(time(nullptr)));
    core0WdtResume();
    if (!verified) { status("SV2 server verification failed"); logf("[pool] SV2: %s\n", transport->noise.error()); return result; }
    logf("[pool] Stratum V2 Noise: server authority verified, identity \"%s\"\n", identity);
    if (!transport->send(client, protocol->setup(netPoolHost().c_str(), netPoolPort(), identity, FW_VERSION))) return result;
    uint32_t start = millis(), lastReply = start; transport->lastRx = start;
    while (client.connected() && WiFi.isConnected()) {
        Frame frame;
        int received = transport->poll(client, frame);
        if (received < 0) { status("SV2 invalid or unauthenticated frame"); return result; }
        if (received) {
            Event event = protocol->handle(frame);
            if (event == Event::Error) {
                if (!result.authorized) result.refusal = protocol->error();
                status("SV2 protocol refused/invalid"); logf("[pool] SV2: %s\n", protocol->error().c_str()); return result;
            }
            if (event == Event::Setup) {
                double rate = minerHashrate();
#ifdef BOARD_GAMMA601
                // Channel negotiation hint only; measured rate still comes from ASIC counters.
                if (!(rate > 0)) rate = 1e12;
#endif
                if (!transport->send(client, protocol->open(settings.btcAddress.c_str(), rate > 0 ? rate : 100000))) return result;
            }
            if (event == Event::Opened) {
                result.authorized = true; minerPoolConnected(true);
                status(netOnBackup() ? "Mining (backup pool)" : "Mining");
                minerPoolDifficulty(protocol->difficulty());
            }
            if (event == Event::Job || (event == Event::Target && protocol->hasJob())) {
                minerPoolDifficulty(protocol->job().difficulty);
                minerPublishJob(protocol->job(), protocol->job().clean);
                static uint32_t loggedHeight = 0;
                if (protocol->job().clean && protocol->job().blockHeight() != loggedHeight) {
                    loggedHeight = protocol->job().blockHeight();
                    logf("[pool] SV2 new block %u, job %s diff=%g\n", loggedHeight, protocol->job().id.c_str(),
                         protocol->job().difficulty);
                }
            }
            if (event == Event::Target && !protocol->hasJob()) minerPoolDifficulty(protocol->difficulty());
            if (event == Event::Accepted || event == Event::Rejected) {
                lastReply = millis();
                for (uint32_t i = 0; i < protocol->acknowledged(); ++i) minerShareResult(event == Event::Accepted);
                if (event == Event::Rejected)
                    logf("[pool] SV2 %u share(s) rejected: %s\n", protocol->acknowledged(), protocol->error().c_str());
            }
        }
        pending = protocol->pending();
        if (!result.authorized && millis() - start > 30000) { status("SV2 setup/channel timed out"); return result; }
        if (millis() - transport->lastRx > 300000) { status("Pool silent, reconnecting"); return result; }
        if (protocol->pending() && millis() - lastReply > 60000) { status("Share acknowledgments timed out"); return result; }
        if (maxMs && millis() - start > maxMs) { result.timedOut = true; status("Switching pool"); return result; }
        stratum::Share share;
        if (protocol->opened() && protocol->pending() < 32 && minerNextShare(share, 20)) {
            bool first = !protocol->pending(); Frame submit = protocol->submit(share);
            if (submit.payload.empty()) { logf("[pool] SV2 obsolete share dropped (job %s)\n", share.jobId); continue; }
            if (first) lastReply = millis();
            if (!transport->send(client, submit)) return result;
            pending = protocol->pending();
        } else vTaskDelay(pdMS_TO_TICKS(10));
    }
    return result;
}
} // namespace sv2
