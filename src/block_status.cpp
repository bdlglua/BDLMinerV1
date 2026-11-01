#include "block_status.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include "miner.h"
#include "notify.h"
#include "tls.h"

namespace {
constexpr const char *kHost = "mempool.space";
constexpr uint32_t kInitialMs = 15000, kPollMs = 120000, kMaxRetryMs = 6 * 3600000;
portMUX_TYPE chainMux = portMUX_INITIALIZER_UNLOCKED;
BlockChainStatus observed;
bool haveTarget = false, notified = false;
uint32_t nextCheck = 0, retryMs = 30000;

// HTTPClient decodes chunking into this fixed-size sink. A long or malformed
// response cannot grow the heap, unlike getString() on an unbounded body.
class Body : public Stream {
public:
    char bytes[513]{};
    size_t used = 0;
    bool overflow = false;
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *data, size_t length) override {
        if (length > sizeof(bytes) - 1 - used) { overflow = true; return 0; }
        memcpy(bytes + used, data, length); used += length; bytes[used] = 0;
        return length;
    }
};
int get(WiFiClientSecure &tls, HTTPClient &http, const char *path, Body &body) {
    String url = String("https://mempool.space/api") + path;
    if (!http.begin(tls, url)) return -1;
    if (!tls.connected() && !tlsConnect(tls, kHost)) { http.end(); return -2; }
    int code = http.GET();
    if (code == 200) {
        if (http.getSize() > 512) code = 413;
        else {
            int result = http.writeToStream(&body);
            if (body.overflow) code = 413;
            else if (result < 0 || !body.used || (http.getSize() >= 0 && int(body.used) != http.getSize())) code = -3;
        }
    }
    http.end();
    return code;
}
bool tipHeight(const char *text, uint32_t &height) {
    if (!*text) return false;
    uint64_t value = 0;
    unsigned count = 0;
    for (; *text >= '0' && *text <= '9'; ++text) {
        if (++count > 10) return false;
        value = value * 10 + (*text - '0');
        if (value > UINT32_MAX) return false;
    }
    while (*text == '\n' || *text == '\r' || *text == ' ') ++text;
    if (!count || *text) return false;
    height = uint32_t(value); return true;
}
}

bool blockStatusGet(const uint8_t hash[32], BlockChainStatus &out) {
    portENTER_CRITICAL(&chainMux);
    bool match = haveTarget && !memcmp(hash, observed.hash, 32);
    if (match) out = observed;
    portEXIT_CRITICAL(&chainMux);
    return match;
}

void blockStatusPoll() {
    BlockCandidate candidate{};
    if (!minerLastBlockCandidate(candidate)) return;
    uint32_t now = millis();
    portENTER_CRITICAL(&chainMux);
    bool changed = !haveTarget || memcmp(candidate.hash, observed.hash, 32);
    if (changed) {
        observed = {}; memcpy(observed.hash, candidate.hash, 32); haveTarget = true;
    }
    BlockChainStatus result = observed;
    portEXIT_CRITICAL(&chainMux);
    if (changed) { notified = false; retryMs = 30000; nextCheck = now + kInitialMs; return; }
    if (int32_t(now - nextCheck) < 0 || !WiFi.isConnected() || !tlsMemoryOk()) return;
    uint8_t id[32]; for (unsigned i = 0; i < 32; ++i) id[i] = candidate.hash[31 - i];
    char path[96]; auto hex = work::toHex(id, 32);
    snprintf(path, sizeof(path), "/block/%s/status", hex.c_str());
    WiFiClientSecure tls;
    HTTPClient http; http.setReuse(true); http.setTimeout(4000);
    Body body;
    int code = get(tls, http, path, body);
    bool success = false;
    if (code == 200) {
        JsonDocument doc;
        auto error = deserializeJson(doc, body.bytes, body.used, DeserializationOption::NestingLimit(3));
        if (!error && doc["in_best_chain"].is<bool>()) {
            bool inChain = doc["in_best_chain"].as<bool>();
            if (!inChain) {
                result.known = true; result.inBestChain = false; result.confirmations = 0;
                result.height = 0; success = true;
            } else if (doc["height"].is<uint32_t>() &&
                       (!candidate.height || doc["height"].as<uint32_t>() == candidate.height)) {
                uint32_t height = doc["height"].as<uint32_t>(), tip;
                Body tipBody;
                code = get(tls, http, "/blocks/tip/height", tipBody);
                if (code == 200 && tipHeight(tipBody.bytes, tip) && tip >= height && uint64_t(tip) - height < UINT32_MAX) {
                    result.known = result.inBestChain = true;
                    result.height = height; result.confirmations = tip - height + 1; success = true;
                } else if (code == 200) code = -4;
            } else code = -4;
        } else code = -4;
    }
    tls.stop();
    result.error = success ? 0 : code;
    result.notFound = !success && code == 404;
    if (success) result.checkedMs = millis();
    // A new candidate can arrive while HTTPS is in flight: never attach the
    // older proof's result to it or send an alert about the wrong candidate.
    BlockCandidate latest{};
    if (!minerLastBlockCandidate(latest) || memcmp(latest.hash, candidate.hash, 32)) return;
    portENTER_CRITICAL(&chainMux);
    observed = result;
    portEXIT_CRITICAL(&chainMux);
    if (success && result.inBestChain && !notified) {
        char message[180];
        snprintf(message, sizeof(message), "mempool.space reports block %u in the Bitcoin best chain (%u confirmations). Pool payout is separate.",
                 (unsigned)result.height, (unsigned)result.confirmations);
        notify("Block seen in chain", message, 5, "link"); notified = true;
    } else if (success && !result.inBestChain && notified) {
        notify("Block chain changed", "mempool.space no longer reports this block in its best chain. Check the explorer and pool.", 4, "warning");
        notified = false;
    }
    if (success) { retryMs = 30000; nextCheck = millis() + (result.confirmations >= 6 ? 600000 : kPollMs); }
    else { nextCheck = millis() + retryMs; retryMs = min<uint32_t>(retryMs * 2, kMaxRetryMs); }
}
