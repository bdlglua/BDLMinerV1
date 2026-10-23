#include "net.h"

#include <WiFi.h>

#include <string>

#include "core/stratum.h"
#include "miner.h"
#include "settings.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace {

constexpr uint32_t kPoolSilenceTimeoutMs = 5 * 60 * 1000;  // no message -> reconnect
constexpr size_t kMaxLine = 16 * 1024;

char status[48] = "Starting";

void setStatus(const char *s) {
    strlcpy(status, s, sizeof(status));
    Serial.printf("[pool] %s\n", s);
}

bool sendLine(WiFiClient &client, const std::string &line) {
    return client.write((const uint8_t *)line.data(), line.size()) == line.size();
}

// Runs one pool session until the connection drops.
void session(WiFiClient &client, stratum::Protocol &proto) {
    std::string buf;
    uint8_t chunk[512];
    uint32_t lastRx = millis();

    if (!sendLine(client, proto.subscribe("NerdMinerV3/" FW_VERSION))) return;

    while (client.connected() && WiFi.isConnected()) {
        int n;
        while ((n = client.read(chunk, sizeof(chunk))) > 0) {
            lastRx = millis();
            for (int i = 0; i < n; i++) {
                char c = (char)chunk[i];
                if (c != '\n') {
                    if (buf.size() < kMaxLine) buf += c;
                    continue;
                }
                if (buf.empty()) continue;

                stratum::Event ev = proto.handleLine(buf.c_str());
                buf.clear();
                switch (ev) {
                    case stratum::Event::Subscribed:
                        if (settings.suggestedDifficulty > 0)
                            sendLine(client, proto.suggestDifficulty(settings.suggestedDifficulty));
                        sendLine(client, proto.authorize());
                        break;
                    case stratum::Event::Authorized:
                        stats.poolConnected = true;
                        setStatus("Mining");
                        break;
                    case stratum::Event::AuthFailed:
                        setStatus("Auth failed: check address");
                        Serial.printf("[pool] error: %s\n", proto.lastError().c_str());
                        return;
                    case stratum::Event::NewJob:
                        minerPublishJob(proto.job(), proto.job().clean);
                        Serial.printf("[pool] job %s height=%u clean=%d diff=%g\n", proto.job().id.c_str(),
                                      proto.job().blockHeight(), proto.job().clean, proto.job().difficulty);
                        break;
                    case stratum::Event::Difficulty:
                        stats.poolDiff = proto.job().difficulty;
                        if (proto.hasJob()) minerPublishJob(proto.job(), false);
                        Serial.printf("[pool] difficulty %g\n", proto.job().difficulty);
                        break;
                    case stratum::Event::ShareAccepted:
                        stats.accepted++;
                        Serial.println("[pool] share accepted");
                        break;
                    case stratum::Event::ShareRejected:
                        stats.rejected++;
                        Serial.printf("[pool] share rejected: %s\n", proto.lastError().c_str());
                        break;
                    default:
                        break;
                }
            }
        }

        if (millis() - lastRx > kPoolSilenceTimeoutMs) {
            setStatus("Pool silent, reconnecting");
            return;
        }
        // Waiting for a share also paces the socket polling.
        stratum::Share share;
        if (minerNextShare(share, 20)) {
            Serial.printf("[pool] submit nonce=%08x diff=%.4g job=%s\n", (unsigned)share.nonce, share.difficulty,
                          share.jobId);
            if (!sendLine(client, proto.submit(share))) return;
        }
    }
}

void netTask(void *) {
    WiFiClient client;
    stratum::Protocol proto;
    uint32_t backoffMs = 2000;

    for (;;) {
        if (!WiFi.isConnected()) {
            setStatus("WiFi lost, reconnecting");
            while (!WiFi.isConnected()) {
                WiFi.reconnect();
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
        }

        char msg[48];
        snprintf(msg, sizeof(msg), "Connecting %s", settings.poolHost.c_str());
        setStatus(msg);
        if (client.connect(settings.poolHost.c_str(), settings.poolPort, 10000)) {
            client.setNoDelay(true);
            proto.reset();
            proto.setCredentials(settings.btcAddress.c_str(), settings.poolPassword.c_str());
            setStatus("Subscribing");
            uint32_t t0 = millis();
            session(client, proto);
            client.stop();
            if (millis() - t0 > 60000) backoffMs = 2000;  // session was healthy
        } else {
            setStatus("Pool unreachable");
        }

        stats.poolConnected = false;
        minerInvalidate();
        if (strcmp(status, "Mining") == 0) setStatus("Disconnected");
        vTaskDelay(pdMS_TO_TICKS(backoffMs));
        backoffMs = min<uint32_t>(backoffMs * 2, 60000);
    }
}

}  // namespace

void netStart() { xTaskCreatePinnedToCore(netTask, "stratum", 12288, nullptr, 3, nullptr, 0); }

const char *netStatus() { return status; }
