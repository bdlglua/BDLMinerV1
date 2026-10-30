#include "share.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>
#include <stdio.h>

#include "log.h"
#include "miner.h"
#include "settings.h"
#include "tls.h"

#ifndef FW_ENV
#define FW_ENV "dev"
#endif

namespace {

// The first report waits for a settled hashrate (the average of the last
// minute) once mining has started; then one an hour, which is what the
// Worker adds up. A failed report is not retried sooner: the next one comes.
constexpr uint32_t kFirstMs = 15 * 60 * 1000UL;
constexpr uint32_t kEveryMs = 3600 * 1000UL;
constexpr uint32_t kWaitMs = 5 * 60 * 1000UL;  // no WiFi, memory or hashrate yet
uint32_t next = kFirstMs;

}  // namespace

size_t shareBody(char *out, size_t size, double hashrate) {
    double rounded = hashrate > 0 && isfinite(hashrate) ? hashrate : 0;
    if (rounded > 0) {
        double scale = pow(10, floor(log10(rounded)) - 2);
        rounded = round(rounded / scale) * scale;
    }
    int n = snprintf(out, size, "{\"v\":\"%s\",\"board\":\"%s\",\"hashrate\":%.3g}", FW_VERSION, FW_ENV, rounded);
    return n > 0 && (size_t)n < size ? (size_t)n : 0;
}

void sharePoll() {
    if (!settings.shareHashrate || (int32_t)(millis() - next) < 0) return;
    double rate = minerHashrate();
    // A paused miner adds nothing: it is not counted either.
    if (!WiFi.isConnected() || !tlsMemoryOk() || !(rate > 0)) {
        next = millis() + kWaitMs;
        return;
    }
    next = millis() + kEveryMs;
    char body[128];
    if (!shareBody(body, sizeof(body), rate)) return;
    String url = STATS_URL;
    int start = url.indexOf("://") + 3, slash = url.indexOf('/', start);
    String host = url.substring(start, slash < 0 ? url.length() : slash);
    WiFiClientSecure tls;
    HTTPClient http;
    http.setTimeout(10000);
    int code = -1;
    if (http.begin(tls, url) && tlsConnect(tls, host.c_str())) {
        http.setUserAgent("BDLMinerV1/" FW_VERSION);
        http.addHeader("Content-Type", "application/json");
        code = http.POST((uint8_t *)body, strlen(body));
    }
    http.end();
    if (code >= 200 && code < 300) logf("[share] hashrate shared (%.3g H/s)\n", rate);
    else logf("[share] hashrate not shared: %s\n", code > 0 ? String("HTTP " + String(code)).c_str() : "no connection");
}
