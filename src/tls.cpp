#include "tls.h"

#include <freertos/semphr.h>

#include "hw_sha.h"
#include "log.h"

// Created before setup() runs, so before any task can race for it.
static SemaphoreHandle_t lock = xSemaphoreCreateMutex();

bool tlsConnect(WiFiClientSecure &tls, const char *host, uint16_t port) {
    tls.setInsecure();
    xSemaphoreTake(lock, portMAX_DELAY);
    // hwsha::hold() pauses the hardware SHA loop on the chips that need it
    // (not the original ESP32), and the key exchange runs for a second or
    // two without yielding: with core 1 left to the hardware miner nothing
    // else can feed core 0's idle task meanwhile, hence the watchdog pause.
    hwsha::hold();
    disableCore0WDT();
    uint32_t t0 = millis();
    bool ok = tls.connect(host, port);
    logf("[tls] %s: handshake %s in %u ms\n", host, ok ? "done" : "failed", (unsigned)(millis() - t0));
    enableCore0WDT();
    hwsha::release();
    xSemaphoreGive(lock);
    return ok;
}
