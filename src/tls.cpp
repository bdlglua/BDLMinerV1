#include "tls.h"

#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <mbedtls/platform.h>
#include <sdkconfig.h>
#if defined(CONFIG_MBEDTLS_SSL_PROTO_TLS1_3)
#include <psa/crypto.h>
#endif

#include <atomic>
#include <cstdint>
#include <ctime>

#include "hw_sha.h"
#include "log.h"
#include "tls_roots.h"

// A certificate is valid between two dates: before NTP has set the clock,
// none can be checked (and none is accepted unchecked).
bool tlsClockSet() { return time(nullptr) >= 1700000000; }

// One handshake at a time. A binary semaphore, not a mutex: FreeRTOS only
// gives a task its own priority back once it holds no mutex at all, so with
// a mutex held across the handshake, the WiFi task (priority 23) waiting a
// moment for a lock mbedtls takes (the bignum hardware) left the TLS task at
// priority 23 until the end of the handshake, 1.5-2 s during which nothing
// else ran on core 0 (screen, web server). Created before setup() runs, so
// before any task can race for it.
static SemaphoreHandle_t lock = [] {
    SemaphoreHandle_t s = xSemaphoreCreateBinary();
    if (s) xSemaphoreGive(s);
    return s;
}();

// mbedtls allocates in internal RAM only (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC),
// and a connection needs a 16.7 KB block for its receive buffer: with the
// heap fragmented (79 KB free, 18 KB largest block), handshakes failed now and
// then with "Memory allocation failed". With PSRAM, buffers of 4 KB and more
// go there; small ones (bignum limbs, hot in the handshake) stay internal.
void *tlsCalloc(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) return nullptr;
    if (n * size >= 4096) {
        void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
        if (p) return p;
    }
    return heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static std::atomic<int> wdtPauses{0};
static std::atomic<bool> certificateRejected{false};
// mbedTLS: a chain that does not verify (expired, wrong name...), or one the
// certificate bundle rejects (no trusted root signed it).
constexpr int kCertificateVerifyFailed = -0x2700, kX509FatalError = -0x3000;
bool tlsCertificateRejected() { return certificateRejected.load(); }
static char lastError[96] = "";  // the 80 of mbedTLS text, plus its code
const char *tlsLastError() { return lastError; }
static int lastErrorCode = 0;
int tlsLastErrorCode() { return lastErrorCode; }
static TlsProfile profile;
static portMUX_TYPE profileMux = portMUX_INITIALIZER_UNLOCKED;

TlsProfile tlsProfile() {
    portENTER_CRITICAL(&profileMux);
    TlsProfile copy = profile;
    portEXIT_CRITICAL(&profileMux);
    return copy;
}

void core0WdtPause() {
    if (wdtPauses++ == 0) disableCore0WDT();
}

void core0WdtResume() {
    if (--wdtPauses == 0) enableCore0WDT();
}

bool tlsMemoryOk() { return ESP.getFreeHeap() >= kTlsMinFree && ESP.getMaxAllocHeap() >= kTlsMinBlock; }

enum class Trust : uint8_t { Bundle, Root, None };

static bool tlsConnectImpl(WiFiClientSecure &tls, const char *host, uint16_t port, uint8_t timeoutSeconds,
                           Trust trust, const char *rootCA = nullptr) {
    certificateRejected = false;
    if (trust != Trust::None && !tlsClockSet()) {
        certificateRejected = true;
        logf("[tls] %s: clock not set yet, its certificate cannot be checked\n", host);
        snprintf(lastError, sizeof(lastError), "clock not set");
        lastErrorCode = 0;
        return false;
    }
    if (trust == Trust::Root) tls.setCACert(rootCA);
    else if (trust == Trust::None) tls.setInsecure();
    tls.setHandshakeTimeout(timeoutSeconds);
    // The TCP connect and reads too. Arduino-ESP32 3.x takes milliseconds
    // here (2.x took seconds): 4 would have meant 4 ms, and every short probe
    // gave up before the server could answer.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    if (timeoutSeconds < 12) tls.setTimeout(timeoutSeconds * 1000UL);
#else
    if (timeoutSeconds < 12) tls.setTimeout(timeoutSeconds);
#endif
    if (!lock || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        logf("[tls] handshake unavailable: TLS lock missing\n");
        return false;
    }
    // The bundle's lookup table is global to the TLS library: only set while
    // holding the lock, for the handshake that uses it.
    // Arduino-ESP32 3.x takes the size too (ESP-IDF 5 checks the bundle).
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    if (trust == Trust::Bundle) tls.setCACertBundle(kTlsRootBundle, sizeof(kTlsRootBundle));
#else
    if (trust == Trust::Bundle) tls.setCACertBundle(kTlsRootBundle);
#endif
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
#if defined(CONFIG_MBEDTLS_SSL_PROTO_TLS1_3)
    // mbedTLS runs TLS 1.3 key exchange on PSA crypto, which must be set up
    // first: ESP-IDF's esp-tls does it before each handshake, Arduino's
    // ssl_client never does, and a server choosing TLS 1.3 then fails the
    // handshake. Initialised once (here, so the hardware SHA is held and the
    // PSRAM allocator above is in place); TLS 1.2 servers never needed it.
    static bool psaReady = false;
    if (!psaReady) {
        psa_status_t psa = psa_crypto_init();
        psaReady = psa == PSA_SUCCESS;
        if (!psaReady) logf("[tls] PSA crypto setup failed (%d): TLS 1.3 servers will refuse\n", (int)psa);
    }
#endif
    uint32_t t0 = millis();
    bool ok = tls.connect(host, port);
    uint32_t elapsed = millis() - t0;
    portENTER_CRITICAL(&profileMux);
    ++profile.handshakes;
    if (!ok) ++profile.failures;
    profile.lastMs = elapsed;
    profile.handshakeMs += elapsed;
    portEXIT_CRITICAL(&profileMux);
    char why[80] = "";
    if (!ok) {
        why[0] = ' ';
        int error = tls.lastError(why + 1, sizeof(why) - 1);
        certificateRejected = trust != Trust::None && (error == kCertificateVerifyFailed || error == kX509FatalError);
        snprintf(lastError, sizeof(lastError), "%s (-0x%04x)", why[1] ? why + 1 : "connection failed", (unsigned)-error);
        lastErrorCode = error;
    } else {
        lastError[0] = 0;
        lastErrorCode = 0;
    }
    // A probe (short timeout) failing is an expected answer: its caller reports the outcome.
    if (ok || timeoutSeconds >= 12)
        logf("[tls] %s: handshake %s in %u ms%s (heap %u, largest block %u)\n", host, ok ? "done" : "failed",
             (unsigned)(millis() - t0), why, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    core0WdtResume();
    hwsha::release();
    xSemaphoreGive(lock);
    return ok;
}

bool tlsConnect(WiFiClientSecure &tls, const char *host, uint16_t port, uint8_t timeoutSeconds) {
    return tlsConnectImpl(tls, host, port, timeoutSeconds, Trust::Bundle);
}
bool tlsConnectVerified(WiFiClientSecure &tls, const char *host, const char *rootCA, uint16_t port, uint8_t timeoutSeconds) {
    return rootCA && *rootCA && tlsConnectImpl(tls, host, port, timeoutSeconds, Trust::Root, rootCA);
}
bool tlsConnectSignedContent(WiFiClientSecure &tls, const char *host, uint16_t port, uint8_t timeoutSeconds) {
    if (tlsConnectImpl(tls, host, port, timeoutSeconds, Trust::Bundle)) return true;
    if (!tlsCertificateRejected()) return false;  // a network failure: no second handshake
    // Only for content whose own signature is checked (firmware releases): a
    // root missing from the bundle, or an unset clock, must not stop updates.
    logf("[tls] %s: certificate not verified; continuing, the firmware signature is checked\n", host);
    tls.stop();
    return tlsConnectImpl(tls, host, port, timeoutSeconds, Trust::None);
}
