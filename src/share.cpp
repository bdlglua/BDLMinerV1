#include "share.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "log.h"
#include "miner.h"
#include "settings.h"
#include "tls.h"

#ifndef FW_ENV
#define FW_ENV "dev"
#endif

namespace {

// The first report waits for a settled hashrate (the average of the last
// minute) once mining has started; then one every 5 minutes, for the live
// statistics. Each report says how long it stands for ("every"), which the
// Worker needs to add them up without an identifier. A failed report is not
// retried sooner: the next one comes.
constexpr uint32_t kFirstMs = 3 * 60 * 1000UL;
constexpr uint32_t kEverySeconds = 300;
constexpr uint32_t kEveryMs = kEverySeconds * 1000UL;
constexpr uint32_t kWaitMs = 60 * 1000UL;  // no WiFi, memory or hashrate yet
uint32_t next = kFirstMs;

}  // namespace

// Names equal but for case, spaces and punctuation.
bool sameName(const char *a, const char *b) {
    for (;;) {
        while (*a && !isalnum((unsigned char)*a)) a++;
        while (*b && !isalnum((unsigned char)*b)) b++;
        if (!*a || !*b) return !*a && !*b;
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
    }
}

size_t shareBody(char *out, size_t size, double hashrate, const char *model) {
    double rounded = hashrate > 0 && isfinite(hashrate) ? hashrate : 0;
    if (rounded > 0) {
        double scale = pow(10, floor(log10(rounded)) - 2);
        rounded = round(rounded / scale) * scale;
    }
    // Model names are fixed strings of the firmware; anything outside letters,
    // digits, spaces and . - + ( ) is left out all the same.
    char safe[40] = "";
    size_t k = 0;
    for (const char *c = model; c && *c && k + 1 < sizeof(safe); c++)
        if (isalnum((unsigned char)*c) || strchr(" .-+()", *c)) safe[k++] = *c;
    safe[k] = 0;
    int n = safe[0] ? snprintf(out, size, "{\"v\":\"%s\",\"board\":\"%s\",\"hashrate\":%.3g,\"every\":%u,\"model\":\"%s\"}",
                               FW_VERSION, FW_ENV, rounded, (unsigned)kEverySeconds, safe)
                    : snprintf(out, size, "{\"v\":\"%s\",\"board\":\"%s\",\"hashrate\":%.3g,\"every\":%u}", FW_VERSION, FW_ENV,
                               rounded, (unsigned)kEverySeconds);
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
    char body[192];
    // The exact model only when it says more than the firmware's own name
    // ("M5StickC Plus2" is the same as "M5StickC-Plus2").
    const char *model = board::modelName();
    if (!shareBody(body, sizeof(body), rate, sameName(model, BOARD_NAME) ? nullptr : model)) return;
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
