#include "tls.h"

#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <mbedtls/platform.h>

#include <atomic>

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

// mbedtls allocates in internal RAM only (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC),
// and a connection needs a 16.7 KB block for its receive buffer: with the
// heap fragmented (79 KB free, 18 KB largest block), handshakes failed now and
// then with "Memory allocation failed". With PSRAM, buffers of 4 KB and more
// go there; small ones (bignum limbs, hot in the handshake) stay internal.
void *tlsCalloc(size_t n, size_t size) {
    if (n * size >= 4096) {
        void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
        if (p) return p;
    }
    return heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static std::atomic<int> wdtPauses{0};

void core0WdtPause() {
    if (wdtPauses++ == 0) disableCore0WDT();
}

void core0WdtResume() {
    if (--wdtPauses == 0) enableCore0WDT();
}

bool tlsConnect(WiFiClientSecure &tls, const char *host, uint16_t port) {
    tls.setInsecure();
    tls.setHandshakeTimeout(12);
    xSemaphoreTake(lock, portMAX_DELAY);
    static bool allocSet = false;
    if (!allocSet) {
        allocSet = true;
        if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
    }
    // hwsha::hold() pauses the hardware SHA loop on the chips that need it
    // (the original ESP32 keeps mining and leaves TLS the software SHA), and
    // the key exchange runs for a second or two without yielding: with core 1
    // left to the hardware miner nothing else can feed core 0's idle task
    // meanwhile, hence the watchdog pause.
    hwsha::hold();
    core0WdtPause();
    uint32_t t0 = millis();
    bool ok = tls.connect(host, port);
    char why[80] = "";
    if (!ok) {
        why[0] = ' ';
        tls.lastError(why + 1, sizeof(why) - 1);
    }
    logf("[tls] %s: handshake %s in %u ms%s (heap %u, largest block %u)\n", host, ok ? "done" : "failed",
         (unsigned)(millis() - t0), why, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    core0WdtResume();
    hwsha::release();
    xSemaphoreGive(lock);
    return ok;
}
