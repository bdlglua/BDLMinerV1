#include "hw_sha.h"

#include <string.h>

#include "core/sha256.h"
#include "core/work.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#if SOC_SHA_SUPPORT_RESUME

#include "hal/sha_ll.h"
#include "sha/sha_dma.h"

namespace hwsha {

bool available() { return true; }

uint32_t nonceAt(uint32_t position) { return position; }

// The SHA/AES lock is recursive, so mbedtls can still take it from the task
// that holds it here, while the miner sees `held` and hashes in software.
static volatile bool held = false;

void hold() {
    held = true;
    esp_sha_acquire_hardware();  // waits for the batch in progress
}

void release() {
    esp_sha_release_hardware();
    held = false;
}

static inline void waitIdle() {
    while (sha_ll_busy()) {
    }
}

void prepare(Job &job, const uint8_t header[80]) {
    // The engine works on raw message bytes, so the header is copied as-is.
    memcpy(job.tail, header + 64, 16);
    memset(&job.tail[4], 0, 48);
    ((uint8_t *)job.tail)[16] = 0x80;
    ((uint8_t *)job.tail)[62] = 0x02;  // 640 bits, big-endian length
    ((uint8_t *)job.tail)[63] = 0x80;

    esp_sha_acquire_hardware();
    sha_ll_fill_text_block(header, 16);
    sha_ll_start_block(SHA2_256);
    waitIdle();
    sha_ll_read_digest(SHA2_256, job.mid, 8);
    esp_sha_release_hardware();
}

uint32_t scan(Job &job, uint32_t first, uint32_t count, uint32_t limit,
              void (*hit)(uint32_t, const uint8_t *, void *), void *ctx) {
    uint32_t second[16] = {0};
    second[8] = 0x00000080;   // padding byte 0x80
    second[15] = 0x00010000;  // 256 bits, big-endian length
    volatile uint32_t *h7 = (volatile uint32_t *)(SHA_H_BASE + 28);

    if (held) return 0;
    esp_sha_acquire_hardware();
    uint32_t nonce = first;
    for (uint32_t i = 0; i < count; i++, nonce++) {
        job.tail[3] = nonce;
        sha_ll_write_digest(SHA2_256, job.mid, 8);
        sha_ll_fill_text_block(job.tail, 16);
        sha_ll_continue_block(SHA2_256);
        waitIdle();
        sha_ll_read_digest(SHA2_256, second, 8);

        sha_ll_fill_text_block(second, 16);
        sha_ll_start_block(SHA2_256);
        waitIdle();
        // Bytes 28..31 of the digest, read as a little-endian word, are the
        // most significant 32 bits of the hash as a 256-bit number.
        if (*h7 <= limit) {
            uint8_t hash[32];
            sha_ll_read_digest(SHA2_256, hash, 8);
            hit(nonce, hash, ctx);
        }
    }
    esp_sha_release_hardware();
    return count;
}

}  // namespace hwsha

#elif CONFIG_IDF_TARGET_ESP32

#include <Arduino.h>
#include <stddef.h>

#include "driver/periph_ctrl.h"
#include "esp_chip_info.h"
#include "sha/sha_parallel_engine.h"
#include "soc/hwcrypto_reg.h"

#define HWSHA_CLASSIC 1

namespace hwsha {

namespace {

// What the loops in hw_sha_classic.S work from (CTX_* offsets there).
struct Ctx {
    uint32_t hdr[20];       // header words as the engine takes them (big-endian)
    uint32_t nonce;         // in: first position; out: the next one
    uint32_t budget;        // in: positions to try; out: positions not tried
    volatile uint32_t run;  // read every nonce: clearing it stops the loop
    uint32_t pad;           // 0x80000000
    uint32_t chk;           // only used by the original's benchmark builds
};
static_assert(offsetof(Ctx, nonce) == 80 && offsetof(Ctx, budget) == 84 && offsetof(Ctx, run) == 88 &&
                  offsetof(Ctx, pad) == 92 && offsetof(Ctx, chk) == 96,
              "layout known to hw_sha_classic.S");

// Returns 1 on a candidate (low 16 bits of the last digest word zero, i.e. the
// top 16 bits of the hash) at position ctx->nonce - 1, 0 when the budget is
// spent or ctx->run was cleared. The nonce word is written to the engine
// as-is, which is why positions are byte-swapped nonces.
typedef uint32_t (*Loop)(volatile uint32_t *text, Ctx *ctx);
extern "C" uint32_t hwsha_classic_fast(volatile uint32_t *text, Ctx *ctx);
extern "C" uint32_t hwsha_classic_margin(volatile uint32_t *text, Ctx *ctx);
extern "C" uint32_t hwsha_classic_wide(volatile uint32_t *text, Ctx *ctx);

// Fastest first (247, 257 and 317 cycles per nonce); the slower ones only
// add timing margin, for chips on which the measured schedule fails.
const struct {
    Loop fn;
    const char *name;
} kLoops[] = {
    {hwsha_classic_fast, "fast"},
    {hwsha_classic_margin, "fast, +2 cycle margins"},
    {hwsha_classic_wide, "wide margins"},
};
constexpr int kLoopCount = sizeof(kLoops) / sizeof(kLoops[0]);
volatile int current = kLoopCount;  // selected loop, kLoopCount = none usable

volatile uint32_t *text() { return (volatile uint32_t *)SHA_TEXT_BASE; }

// Known answer, checked on a PC with the software kernel: for the header
// words 0x9E3779B9 * (i + 1), these are the only candidates among the 262144
// positions from 0x1000. One wrong hash that happens to be (or to hide) a
// candidate fails the test.
bool knownAnswer(Loop fn) {
    static const uint32_t expected[] = {0xa94c, 0xe496, 0x1f476, 0x302f0, 0x35af9, 0x3cf5c};
    const size_t n = sizeof(expected) / sizeof(expected[0]);
    Ctx c{};
    for (int i = 0; i < 19; i++) c.hdr[i] = 0x9E3779B9u * (i + 1);
    c.nonce = 0x1000;
    c.budget = 262144;
    c.run = 1;
    c.pad = 0x80000000u;
    size_t found = 0;
    while (c.budget > 0 && fn(text(), &c)) {
        if (found == n || c.nonce - 1 - 0x1000 != expected[found]) return false;
        found++;
    }
    return found == n && c.budget == 0;
}

// Picks the fastest loop this chip runs correctly.
bool pickLoop() {
    // Before revision 3, a raw DPORT read on one core (the loops read the
    // digest that way) can corrupt APB reads on the other one: WiFi and UART
    // die within seconds, which no self-test can see.
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    if (chip.full_revision / 100 < 3) {
        Serial.printf("[miner] ESP32 revision %u.%u: the hardware SHA loop needs revision 3\n",
                      chip.full_revision / 100, chip.full_revision % 100);
        return false;
    }

    esp_sha_lock_engine(SHA2_256);
    periph_module_reset(PERIPH_SHA_MODULE);
    int pick = 0;
    // Best of three: one stray misread must not cost the fast loop for good.
    while (pick < kLoopCount && !knownAnswer(kLoops[pick].fn) && !knownAnswer(kLoops[pick].fn) &&
           !knownAnswer(kLoops[pick].fn)) {
        Serial.printf("[miner] hardware SHA loop '%s' failed its self-test\n", kLoops[pick].name);
        pick++;
    }
    esp_sha_unlock_engine(SHA2_256);
    current = pick;
    if (pick < kLoopCount) Serial.printf("[miner] hardware SHA loop: %s\n", kLoops[pick].name);
    return pick < kLoopCount;
}

// Every candidate is recomputed in software, so the share of candidates that
// software does not confirm measures how many hashes the loop gets wrong. It
// is 0 when healthy; otherwise fall back to the next, slower loop, and to
// software alone after the last one.
void health(bool confirmed) {
    static uint32_t hits = 0, wrong = 0;
    hits++;
    if (!confirmed) wrong++;
    if (hits < 256) return;
    if (wrong >= 8) {
        int next = current + 1;
        Serial.printf("[miner] %u of %u hardware SHA candidates were wrong, switching to: %s\n", (unsigned)wrong,
                      (unsigned)hits, next < kLoopCount ? kLoops[next].name : "software");
        current = next;
    }
    hits = wrong = 0;
}

}  // namespace

bool available() { return true; }

uint32_t nonceAt(uint32_t position) { return __builtin_bswap32(position); }

// mbedtls only ever try-locks the engine and falls back to software SHA, so
// holding it here never blocks a TLS session. `held` keeps the miner from
// taking the engine back between two batches before hold() gets it.
static volatile bool held = false;

void hold() {
    held = true;
    esp_sha_lock_engine(SHA2_256);  // waits for the batch in progress
}

void release() {
    esp_sha_unlock_engine(SHA2_256);
    held = false;
}

void prepare(Job &job, const uint8_t header[80]) {
    for (int i = 0; i < 20; i++)
        job.words[i] = (uint32_t)header[4 * i] << 24 | (uint32_t)header[4 * i + 1] << 16 |
                       (uint32_t)header[4 * i + 2] << 8 | header[4 * i + 3];
    sha::prepare(job.check, header);
}

uint32_t scan(Job &job, uint32_t first, uint32_t count, uint32_t limit,
              void (*hit)(uint32_t, const uint8_t *, void *), void *ctx) {
    int loop = current;
    if (loop >= kLoopCount || held) return 0;
    // An open TLS connection can hold the engine for seconds (mbedtls then
    // keeps it for the whole digest); never wait for it.
    if (!esp_sha_try_lock_engine(SHA2_256)) return 0;
    // Start from a clean peripheral, whatever used it last.
    periph_module_reset(PERIPH_SHA_MODULE);

    Ctx c;
    memcpy(c.hdr, job.words, sizeof(c.hdr));
    c.nonce = first;
    c.budget = count;
    c.run = 1;
    c.pad = 0x80000000u;
    while (c.budget > 0 && kLoops[loop].fn(text(), &c)) {
        // The loop only looked at 16 bits of the digest: recompute the hash.
        uint32_t nonce = nonceAt(c.nonce - 1);
        uint8_t hash[32];
        bool confirmed = sha::scan(job.check, nonce, 0x0000FFFF, hash);
        health(confirmed);
        if (confirmed) {
            uint32_t top = (uint32_t)hash[28] | (uint32_t)hash[29] << 8 | (uint32_t)hash[30] << 16 |
                           (uint32_t)hash[31] << 24;
            if (top <= limit) hit(nonce, hash, ctx);
        }
    }
    esp_sha_unlock_engine(SHA2_256);
    return count - c.budget;
}

}  // namespace hwsha

#else  // no usable SHA peripheral

namespace hwsha {

bool available() { return false; }
uint32_t nonceAt(uint32_t position) { return position; }
void hold() {}
void release() {}
void prepare(Job &, const uint8_t *) {}
uint32_t scan(Job &, uint32_t, uint32_t, uint32_t, void (*)(uint32_t, const uint8_t *, void *), void *) {
    return 0;
}

}  // namespace hwsha

#endif

namespace hwsha {

namespace {
struct TestCtx {
    uint32_t nonce;
    uint8_t hash[32];
    int hits;
};
}  // namespace

bool selfTest() {
    if (!available()) return false;
#ifdef HWSHA_CLASSIC
    if (!pickLoop()) return false;
#endif

    static const char *kGenesis =
        "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e"
        "67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c";
    const uint32_t kNonce = 2083236893;
    uint8_t header[80];
    work::fromHex(kGenesis, header, 80);

    Job job;
    prepare(job, header);
    TestCtx t{};
    // nonceAt() is its own inverse: this is the genesis nonce's position.
    uint32_t pos = nonceAt(kNonce);
    scan(job, pos - 1000, 2000, work::quickLimitFor(1.0),
         [](uint32_t nonce, const uint8_t hash[32], void *ctx) {
             TestCtx *t = (TestCtx *)ctx;
             t->nonce = nonce;
             memcpy(t->hash, hash, 32);
             t->hits++;
         },
         &t);
    if (t.hits != 1 || t.nonce != kNonce) return false;

    uint8_t ref[32];
    sha::sha256d(header, 80, ref);
    return memcmp(ref, t.hash, 32) == 0;
}

}  // namespace hwsha
