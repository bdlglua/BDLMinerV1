#include "market.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "hw_sha.h"
#include "settings.h"

namespace {

constexpr const char *kApi = "https://mempool.space/api";
constexpr uint32_t kRefreshMs = 5 * 60 * 1000;
constexpr uint32_t kRetryMs = 30 * 1000;
// A TLS session needs ~45 KB; never starve the miner and the pool socket.
constexpr uint32_t kMinHeapForTls = 70 * 1024;

MarketData data;
portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

// GET `path` and parse the JSON body through `filter` (keeps RAM low: large
// arrays such as the mempool fee histogram are skipped while streaming).
bool fetch(WiFiClientSecure &tls, const char *path, JsonDocument &doc, const JsonDocument &filter) {
    // Keeps the hardware SHA loop out of the way of the TLS session.
    struct Hold {
        Hold() { hwsha::hold(); }
        ~Hold() { hwsha::release(); }
    } hold;
    HTTPClient http;
    String url = String(kApi) + path;
    http.useHTTP10(true);  // no chunked encoding: the body can be parsed as a stream
    http.setTimeout(10000);
    if (!http.begin(tls, url)) return false;
    int code = http.GET();
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        DeserializationError err =
            deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        ok = !err;
        if (err) Serial.printf("[market] %s: %s\n", path, err.c_str());
    } else {
        Serial.printf("[market] %s: HTTP %d\n", path, code);
    }
    http.end();
    return ok;
}

// One refresh of every endpoint. Fields that fail keep their previous value.
bool refresh() {
    if (ESP.getFreeHeap() < kMinHeapForTls) {
        Serial.printf("[market] skipped, low memory (%u bytes free)\n", (unsigned)ESP.getFreeHeap());
        return false;
    }
    WiFiClientSecure tls;
    tls.setInsecure();  // public read-only data, no secret is sent
    MarketData d = marketGet();
    int ok = 0;

    {
        JsonDocument filter, doc;
        filter[settings.currency.c_str()] = true;
        if (fetch(tls, "/v1/prices", doc, filter)) {
            d.price = doc[settings.currency.c_str()] | 0.0;
            ok++;
        }
    }
    {
        JsonDocument filter, doc;
        filter["fastestFee"] = true;
        filter["halfHourFee"] = true;
        filter["hourFee"] = true;
        if (fetch(tls, "/v1/fees/recommended", doc, filter)) {
            d.feeFast = doc["fastestFee"] | 0;
            d.feeHalfHour = doc["halfHourFee"] | 0;
            d.feeHour = doc["hourFee"] | 0;
            ok++;
        }
    }
    {
        JsonDocument filter, doc;
        filter["currentHashrate"] = true;
        if (fetch(tls, "/v1/mining/hashrate/3d", doc, filter)) {
            d.networkHashrate = doc["currentHashrate"] | 0.0;
            ok++;
        }
    }
    {
        JsonDocument filter, doc;
        filter["progressPercent"] = true;
        filter["difficultyChange"] = true;
        filter["remainingBlocks"] = true;
        if (fetch(tls, "/v1/difficulty-adjustment", doc, filter)) {
            d.retargetProgress = doc["progressPercent"] | 0.0;
            d.retargetChange = doc["difficultyChange"] | 0.0;
            d.retargetBlocks = doc["remainingBlocks"] | 0;
            ok++;
        }
    }
    {
        JsonDocument filter, doc;
        filter["count"] = true;
        if (fetch(tls, "/mempool", doc, filter)) {
            d.mempoolTx = doc["count"] | 0;
            ok++;
        }
    }

    if (ok) d.updatedMs = millis();
    portENTER_CRITICAL(&dataMux);
    data = d;
    portEXIT_CRITICAL(&dataMux);
    Serial.printf("[market] %d/5 ok | 1 BTC = %.0f %s | fees %u/%u/%u | net %.3g H/s | retarget %+.2f%% in %u\n", ok,
                  d.price, settings.currency.c_str(), d.feeFast, d.feeHalfHour, d.feeHour, d.networkHashrate,
                  d.retargetChange, (unsigned)d.retargetBlocks);
    return ok == 5;
}

void marketTask(void *) {
    for (;;) {
        bool full = WiFi.isConnected() && refresh();
        vTaskDelay(pdMS_TO_TICKS(full ? kRefreshMs : kRetryMs));
    }
}

}  // namespace

void marketStart() {
    configTzTime(settings.timezone.c_str(), "pool.ntp.org", "time.cloudflare.com");
    xTaskCreatePinnedToCore(marketTask, "market", 10240, nullptr, 2, nullptr, 0);
}

MarketData marketGet() {
    portENTER_CRITICAL(&dataMux);
    MarketData d = data;
    portEXIT_CRITICAL(&dataMux);
    return d;
}

bool clockNow(struct tm &out) {
    time_t now = time(nullptr);
    if (now < 1700000000) return false;  // not synchronised yet
    localtime_r(&now, &out);
    return true;
}
