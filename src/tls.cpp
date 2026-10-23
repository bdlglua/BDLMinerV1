#include "tls.h"

#include <freertos/semphr.h>

#include "hw_sha.h"

// Created before setup() runs, so before any task can race for it.
static SemaphoreHandle_t lock = xSemaphoreCreateMutex();

bool tlsConnect(WiFiClientSecure &tls, const char *host, uint16_t port) {
    tls.setInsecure();
    xSemaphoreTake(lock, portMAX_DELAY);
    // The handshake keeps the hardware SHA loop away (see hwsha::hold()), and
    // its key exchange runs for seconds without yielding: with core 1 left to
    // the hardware miner nothing else can feed core 0's idle task meanwhile.
    hwsha::hold();
    disableCore0WDT();
    bool ok = tls.connect(host, port);
    enableCore0WDT();
    hwsha::release();
    xSemaphoreGive(lock);
    return ok;
}
