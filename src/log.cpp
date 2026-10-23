#include "log.h"

#include <Preferences.h>
#include <esp_system.h>
#include <stdarg.h>

namespace {

constexpr size_t kRing = 6144;
char ring[kRing];
size_t head = 0;   // next write position
bool wrapped = false;
portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;
char reason[96] = "unknown";

void append(const char *s, size_t n) {
    portENTER_CRITICAL(&ringMux);
    for (size_t i = 0; i < n; i++) {
        ring[head++] = s[i];
        if (head == kRing) {
            head = 0;
            wrapped = true;
        }
    }
    portEXIT_CRITICAL(&ringMux);
}

const char *resetName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "power on";
        case ESP_RST_EXT: return "reset pin";
        case ESP_RST_SW: return "software restart";
        case ESP_RST_PANIC: return "crash (panic)";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake-up";
        case ESP_RST_BROWNOUT: return "brownout (power supply too weak)";
        case ESP_RST_SDIO: return "SDIO";
        default: return "unknown";
    }
}

}  // namespace

void logf(const char *fmt, ...) {
    char line[256];
    uint32_t s = millis() / 1000;
    int n = snprintf(line, sizeof(line), "%02u:%02u:%02u ", (unsigned)(s / 3600 % 100), (unsigned)(s / 60 % 60),
                     (unsigned)(s % 60));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - n, fmt, ap);
    va_end(ap);
    Serial.print(line + n);  // the serial monitor keeps its own timestamps if wanted
    append(line, strlen(line));
}

String logText() {
    String out;
    out.reserve(kRing);
    portENTER_CRITICAL(&ringMux);
    size_t start = wrapped ? head : 0, len = wrapped ? kRing : head;
    static char copy[kRing];
    for (size_t i = 0; i < len; i++) copy[i] = ring[(start + i) % kRing];
    portEXIT_CRITICAL(&ringMux);
    size_t from = 0;
    if (wrapped)  // drop the partial first line
        while (from < len && copy[from++] != '\n') {
        }
    out.concat(copy + from, len - from);
    return out;
}

void logBoot() {
    Preferences p;
    p.begin("boot", false);
    String why = p.getString("why", "");
    if (why.length()) p.remove("why");
    p.end();
    esp_reset_reason_t r = esp_reset_reason();
    if (r == ESP_RST_SW && why.length()) snprintf(reason, sizeof(reason), "restarted by the miner: %s", why.c_str());
    else snprintf(reason, sizeof(reason), "%s", resetName(r));
    logf("[boot] last reset: %s\n", reason);
}

const char *bootReason() { return reason; }

void setRestartReason(const char *why) {
    Preferences p;
    p.begin("boot", false);
    p.putString("why", why);
    p.end();
}
