#include "tls.h"

#include <freertos/semphr.h>

#include "hw_sha.h"
#include "log.h"

// One handshake at a time. A binary semaphore, not a mutex: FreeRTOS only
// gives a task its own priority back once it holds no mutex at all, so with
// a mutex held across the handshake, the WiFi task (priority 23) waiting a
// moment for a lock mbedtls takes (the bignum hardware) left the TLS task at
// priority 23 until the end of the handshake, 1.5-2 s during which nothing
// else ran on core 0 (screen, web server). Created before setup() runs, so
// before any task can race for it.
static SemaphoreHandle_t lock = [] {
    SemaphoreHandle_t s = xSemaphoreCreateBinary();
    xSemaphoreGive(s);
    return s;
}();

bool tlsConnect(WiFiClientSecure &tls, const char *host, uint16_t port) {
    tls.setInsecure();
    xSemaphoreTake(lock, portMAX_DELAY);
    // hwsha::hold() pauses the hardware SHA loop on the chips that need it
    // (the original ESP32 keeps mining and leaves TLS the software SHA), and
    // the key exchange runs for a second or two without yielding: with core 1
    // left to the hardware miner nothing else can feed core 0's idle task
    // meanwhile, hence the watchdog pause.
    hwsha::hold();
    disableCore0WDT();
    uint32_t t0 = millis();
    bool ok = tls.connect(host, port);
    char why[80] = "";
    if (!ok) {
        why[0] = ' ';
        tls.lastError(why + 1, sizeof(why) - 1);
    }
    logf("[tls] %s: handshake %s in %u ms%s\n", host, ok ? "done" : "failed", (unsigned)(millis() - t0), why);
    enableCore0WDT();
    hwsha::release();
    xSemaphoreGive(lock);
    return ok;
}
