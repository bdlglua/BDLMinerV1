#include "hw_sha.h"

#include <string.h>

#include "core/sha256.h"
#include "core/work.h"
#include "log.h"
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

bool fillAvailable() { return false; }
void prepareFill(Fill &, const uint8_t *) {}

uint32_t scan(Job &job, uint32_t first, uint32_t count, uint32_t limit,
              void (*hit)(uint32_t, const uint8_t *, void *), void *ctx, Fill *) {
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

#include <xtensa/hal.h>

#include "core/fill.h"
#include "driver/periph_ctrl.h"
#include "esp_chip_info.h"
#include "sha/sha_parallel_engine.h"
#include "soc/hwcrypto_reg.h"

#define HWSHA_CLASSIC 1

namespace hwsha {

namespace {

// What the loops in hw_sha_classic.S work from (CTX_* offsets there); the
// fields from fillP on are only used by hw_sha_fill.S (CTX in
// tools/hw_fill/gen_hw_fill.py). Addresses are 32-bit on the ESP32.
struct Ctx {
    uint32_t hdr[20];       // header words as the engine takes them (big-endian)
    uint32_t nonce;         // in: first position; out: the next one
    uint32_t budget;        // in: positions to try; out: positions not tried
    volatile uint32_t run;  // read every nonce: clearing it stops the loop
    uint32_t pad;           // 0x80000000
    uint32_t chk;           // only used by the original's benchmark builds
    uint32_t fillP;         // software hash: current record
    uint32_t q;             // software hash: position of the nonce in progress
    uint32_t hitQ, hitF;    // software candidate: position, and set when there is one
    uint32_t end1, end2;    // last record of each hash
    uint32_t b1, b2, b1_61; // first record of each hash, record 61 of the first
    uint32_t iv7, c18;
    uint32_t mid[8];
};
static_assert(offsetof(Ctx, nonce) == 80 && offsetof(Ctx, budget) == 84 && offsetof(Ctx, run) == 88 &&
                  offsetof(Ctx, pad) == 92 && offsetof(Ctx, chk) == 96,
              "layout known to hw_sha_classic.S");
static_assert(offsetof(Ctx, fillP) == 100 && offsetof(Ctx, q) == 104 && offsetof(Ctx, hitQ) == 108 &&
                  offsetof(Ctx, hitF) == 112 && offsetof(Ctx, end1) == 116 && offsetof(Ctx, b1) == 124 &&
                  offsetof(Ctx, b1_61) == 132 && offsetof(Ctx, iv7) == 136 && offsetof(Ctx, c18) == 140 &&
                  offsetof(Ctx, mid) == 144,
              "layout known to hw_sha_fill.S");

// Returns 1 on a candidate (low 16 bits of the last digest word zero, i.e. the
// top 16 bits of the hash) at position ctx->nonce - 1, 0 when the budget is
// spent or ctx->run was cleared. The nonce word is written to the engine
// as-is, which is why positions are byte-swapped nonces.
typedef uint32_t (*Loop)(volatile uint32_t *text, Ctx *ctx);
extern "C" uint32_t hwsha_classic_fast(volatile uint32_t *text, Ctx *ctx);
extern "C" uint32_t hwsha_classic_margin(volatile uint32_t *text, Ctx *ctx);
extern "C" uint32_t hwsha_classic_wide(volatile uint32_t *text, Ctx *ctx);
// The fast loop with a software hash in its idle cycles (hw_sha_fill.S).
extern "C" uint32_t hwsha_classic_fill(volatile uint32_t *text, Ctx *ctx);

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

// The ESP32's SHA-1, SHA-256 and SHA-384/512 engines are locked one by one
// but share the TEXT registers the loops write: all three are taken while a
// loop runs, so mbedtls (TLS) hashes in software meanwhile instead of writing
// TEXT under the loop (which made every TLS handshake fail). Always in this
// order: no deadlock with code that takes them one at a time.
const esp_sha_type kEngines[] = {SHA1, SHA2_256, SHA2_384};

bool tryLockEngines() {
    for (int i = 0; i < 3; i++)
        if (!esp_sha_try_lock_engine(kEngines[i])) {
            while (i--) esp_sha_unlock_engine(kEngines[i]);
            return false;
        }
    return true;
}

void lockEngines() {
    for (esp_sha_type e : kEngines) esp_sha_lock_engine(e);
}

void unlockEngines() {
    for (esp_sha_type e : kEngines) esp_sha_unlock_engine(e);
}

// ---------------------------------------------------------------------------
// Idle-cycle software hashing (hw_sha_fill.S)
// ---------------------------------------------------------------------------

fill::Buffers fillBuf;
volatile bool fillOk = false;

uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

// Points the generated code at the buffers and resumes `fill`.
void fillLoad(Ctx &c, const Fill &f) {
    c.fillP = f.p;
    c.q = f.q;
    c.hitF = 0;
    c.end1 = addr(&fillBuf.b1[fill::kSteps * fill::kRecordWords]);
    c.end2 = addr(&fillBuf.b2[fill::kSteps * fill::kRecordWords]);
    c.b1 = addr(fillBuf.b1);
    c.b2 = addr(fillBuf.b2);
    c.b1_61 = addr(&fillBuf.b1[61 * fill::kRecordWords]);
    c.iv7 = sha::IV[7];
    c.c18 = fillBuf.c18;
    memcpy(c.mid, fillBuf.mid, sizeof(c.mid));
}

void fillStart(Fill &f, const uint8_t header[80], uint32_t first) {
    fill::prepare(fillBuf, header);
    sha::prepare(f.check, header);
    // As after a second hash: the first iteration starts nonce `first`.
    f.p = addr(&fillBuf.b2[fill::kSteps * fill::kRecordWords]);
    f.q = first - 1;
    f.ready = true;
}

void kat(uint32_t words[20], uint8_t header[80]) {
    for (int i = 0; i < 20; i++) {
        uint32_t w = i < 19 ? 0x9E3779B9u * (i + 1) : 0;
        if (words) words[i] = w;
        if (header) {
            header[4 * i] = w >> 24;
            header[4 * i + 1] = w >> 16;
            header[4 * i + 2] = w >> 8;
            header[4 * i + 3] = w;
        }
    }
}

// The hardware part of hw_sha_fill.S is the fast loop instruction for
// instruction, so knownAnswer() must pass with the software hash running,
// which must meanwhile find the one candidate of its range.
bool fillSelfTest() {
    static const uint32_t expected[] = {0xa94c, 0xe496, 0x1f476, 0x302f0, 0x35af9, 0x3cf5c};
    uint8_t header[80];
    Ctx c{};
    kat(c.hdr, header);
    Fill f;
    fillStart(f, header, 0x1000 + 0xa94c - 3);
    fillLoad(c, f);
    c.nonce = 0x1000;
    c.budget = 262144;
    c.run = 1;
    c.pad = 0x80000000u;
    size_t found = 0;
    int fillHits = 0, wrongHw = 0;
    uint32_t firstHit = 0, q0 = c.q;
    while (c.budget > 0) {
        bool hw = hwsha_classic_fill(text(), &c);
        if (c.hitF) {
            if (!fillHits++) firstHit = c.hitQ;
            c.hitF = 0;
        }
        if (!hw) break;
        if (found >= 6 || c.nonce - 1 - 0x1000 != expected[found]) wrongHw++;
        found++;
    }
    bool ok = !wrongHw && found == 6 && c.budget == 0 && fillHits == 1 && firstHit == 0x1000 + 0xa94c;
    if (!ok)
        logf("[fill] self-test: hardware %u candidates (%d wrong, want 6), budget left %u; software %u nonces, "
             "%d candidates (first %08x, want 0000b94c)\n",
             (unsigned)found, wrongHw, (unsigned)c.budget, (unsigned)(c.q - q0), fillHits, (unsigned)firstHit);
    return ok;
}

// After a failed self-test: runs the software hash for exactly one nonce (64
// hardware nonces) and compares every round with a reference computed here.
void fillDiagnose() {
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    auto rounds = [&](const uint32_t st[8], const uint32_t w[64], uint32_t A[65], uint32_t E[65]) {
        uint32_t a = st[0], b = st[1], c = st[2], d = st[3], e = st[4], f = st[5], g = st[6], h = st[7];
        for (int i = 0; i <= 64; i++) {
            A[i] = a;
            E[i] = e;
            if (i == 64) break;
            uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + (g ^ (e & (f ^ g))) + sha::K[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) | (c & (a | b)));
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
    };
    auto expand = [&](uint32_t w[64]) {
        for (int i = 16; i < 64; i++)
            w[i] = (rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
                   (rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
    };

    uint8_t header[80];
    Ctx c{};
    kat(c.hdr, header);
    const uint32_t q = 0x1000 + 0xa94c;  // a candidate
    Fill fl;
    fillStart(fl, header, q);
    fillLoad(c, fl);
    c.nonce = 0x40000000u;
    c.budget = 64;  // transition B, 31 iterations, transition A, 31 iterations
    c.run = 1;
    c.pad = 0x80000000u;
    while (c.budget > 0 && hwsha_classic_fill(text(), &c)) {
    }
    logf("[fill] after 64 iterations: q %08x (want %08x), P %d records from the end of hash 2 (want 0)\n",
         (unsigned)c.q, (unsigned)q, ((int)c.fillP - (int)c.end2) / fill::kRecordBytes);

    sha::MidState ms;
    sha::prepare(ms, header);
    uint32_t w[64] = {ms.w[0], ms.w[1], ms.w[2], q, 0x80000000};
    w[15] = 640;
    expand(w);
    uint32_t A[65], E[65];
    rounds(ms.mid, w, A, E);
    const uint32_t *b1 = fillBuf.b1;
    int bad = -1;
    for (int k = 3; k < 64 && bad < 0; k++)
        if (b1[k * 5 + 2] != w[k]) bad = k;
    logf("[fill] hash 1 message words: %s %d\n", bad < 0 ? "all right" : "first wrong at", bad);
    bad = -1;
    for (int k = 4; k <= 64 && bad < 0; k++)
        if (b1[k * 5] != A[k] || b1[k * 5 + 1] != E[k]) bad = k;
    if (bad >= 0)
        logf("[fill] hash 1 state: first wrong before round %d: a %08x/%08x e %08x/%08x\n", bad,
             (unsigned)b1[bad * 5], (unsigned)A[bad], (unsigned)b1[bad * 5 + 1], (unsigned)E[bad]);
    else
        logf("[fill] hash 1 state: all right\n");

    uint32_t h1[8] = {A[64], A[63], A[62], A[61], E[64], E[63], E[62], E[61]};
    uint32_t w2[64] = {0};
    for (int k = 0; k < 8; k++) w2[k] = ms.mid[k] + h1[k];
    w2[8] = 0x80000000;
    w2[15] = 256;
    expand(w2);
    uint32_t A2[65], E2[65];
    rounds(sha::IV, w2, A2, E2);
    const uint32_t *b2 = fillBuf.b2;  // record k at k + 3
    bad = -1;
    for (int k = 0; k < 62 && bad < 0; k++)
        if (b2[(k + 3) * 5 + 2] != w2[k]) bad = k;
    logf("[fill] hash 2 message words: %s %d\n", bad < 0 ? "all right" : "first wrong at", bad);
    bad = -1;
    for (int k = 1; k <= 62 && bad < 0; k++)
        if (b2[(k + 3) * 5] != A2[k] || b2[(k + 3) * 5 + 1] != E2[k]) bad = k;
    if (bad >= 0)
        logf("[fill] hash 2 state: first wrong before round %d: a %08x/%08x e %08x/%08x\n", bad,
             (unsigned)b2[(bad + 3) * 5], (unsigned)A2[bad], (unsigned)b2[(bad + 3) * 5 + 1], (unsigned)E2[bad]);
    else
        logf("[fill] hash 2 state: all right (candidate: %s)\n",
             ((E2[61] + sha::IV[7]) & 0xFFFF) == 0 ? "yes" : "no");
}

// Cycles for `n` hardware nonces with `fn`, the best of two runs.
uint32_t timeLoop(Loop fn, uint32_t n, uint32_t *fillNonces) {
    uint32_t best = UINT32_MAX;
    for (int run = 0; run < 2; run++) {
        uint8_t header[80];
        Ctx c{};
        kat(c.hdr, header);
        Fill f;
        fillStart(f, header, 0x80000000u);
        fillLoad(c, f);
        c.nonce = 0x40000000u;  // a candidate only returns early: the loop goes on
        c.budget = n;
        c.run = 1;
        c.pad = 0x80000000u;
        uint32_t t0 = xthal_get_ccount();
        while (c.budget > 0 && fn(text(), &c)) {
        }
        uint32_t t = xthal_get_ccount() - t0;
        if (t < best) best = t;
        if (fillNonces) *fillNonces = c.q - (0x80000000u - 1);
    }
    return best;
}

// Uses idle-cycle hashing only when it is correct and makes the total faster.
void pickFill() {
    if (!fillSelfTest() && !fillSelfTest()) {
        logf("[miner] idle-cycle software hashing failed its self-test, disabled\n");
        fillDiagnose();
        return;
    }
    const uint32_t n = 131072;
    uint32_t fillNonces = 0;
    uint32_t plain = timeLoop(hwsha_classic_fast, n, nullptr);
    uint32_t mixed = timeLoop(hwsha_classic_fill, n, &fillNonces);
    double mhz = getCpuFrequencyMhz();
    double hwPlain = n * mhz * 1e3 / plain, hwMixed = n * mhz * 1e3 / mixed;  // kH/s
    double extra = fillNonces * mhz * 1e3 / mixed;
    fillOk = hwMixed + extra > hwPlain * 1.002;
    logf("[miner] idle-cycle software hashing: %s (hardware %.1f -> %.1f kH/s, +%.1f kH/s software)\n",
         fillOk ? "on" : "off, no gain", hwPlain, hwMixed, extra);
}

// Picks the fastest loop this chip runs correctly.
bool pickLoop() {
    // Before revision 3, a raw DPORT read on one core (the loops read the
    // digest that way) can corrupt APB reads on the other one: WiFi and UART
    // die within seconds, which no self-test can see.
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    if (chip.full_revision / 100 < 3) {
        logf("[miner] ESP32 revision %u.%u: the hardware SHA loop needs revision 3\n",
             chip.full_revision / 100, chip.full_revision % 100);
        return false;
    }

    lockEngines();
    periph_module_reset(PERIPH_SHA_MODULE);
    int pick = 0;
    // Best of three: one stray misread must not cost the fast loop for good.
    while (pick < kLoopCount && !knownAnswer(kLoops[pick].fn) && !knownAnswer(kLoops[pick].fn) &&
           !knownAnswer(kLoops[pick].fn)) {
        logf("[miner] hardware SHA loop '%s' failed its self-test\n", kLoops[pick].name);
        pick++;
    }
    current = pick;
    if (pick < kLoopCount) logf("[miner] hardware SHA loop: %s\n", kLoops[pick].name);
    if (pick == 0) pickFill();  // built on the fast loop only
    unlockEngines();
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
        logf("[miner] %u of %u hardware SHA candidates were wrong, switching to: %s\n", (unsigned)wrong,
             (unsigned)hits, next < kLoopCount ? kLoops[next].name : "software");
        current = next;
    }
    hits = wrong = 0;
}

}  // namespace

bool available() { return true; }

uint32_t nonceAt(uint32_t position) { return __builtin_bswap32(position); }

// Nothing to do: the loops hold all three SHA engines (see lockEngines()), so
// mbedtls hashes in software and a TLS handshake runs alongside them. (A
// pause used to be needed while only SHA-256 was locked: mbedtls wrote the
// shared TEXT registers through the SHA-1 / SHA-384 engines meanwhile, and
// every handshake failed.)
void hold() {}
void release() {}

bool fillAvailable() { return fillOk; }

void prepareFill(Fill &f, const uint8_t header[80]) { fillStart(f, header, 0); }

void prepare(Job &job, const uint8_t header[80]) {
    for (int i = 0; i < 20; i++)
        job.words[i] = (uint32_t)header[4 * i] << 24 | (uint32_t)header[4 * i + 1] << 16 |
                       (uint32_t)header[4 * i + 2] << 8 | header[4 * i + 3];
    sha::prepare(job.check, header);
}

// Hands a software candidate of the idle-cycle hash over, once confirmed.
void fillHit(Ctx &c, Fill &f, uint32_t limit, void (*hit)(uint32_t, const uint8_t *, void *)) {
    if (!c.hitF) return;
    c.hitF = 0;
    uint32_t nonce = nonceAt(c.hitQ);
    uint8_t hash[32];
    if (!sha::scan(f.check, nonce, 0x0000FFFF, hash)) {
        fillOk = false;
        logf("[miner] idle-cycle software hashing reported a wrong candidate, disabled\n");
        return;
    }
    uint32_t top = (uint32_t)hash[28] | (uint32_t)hash[29] << 8 | (uint32_t)hash[30] << 16 | (uint32_t)hash[31] << 24;
    if (top <= limit) hit(nonce, hash, f.ctx);
}

uint32_t scan(Job &job, uint32_t first, uint32_t count, uint32_t limit,
              void (*hit)(uint32_t, const uint8_t *, void *), void *ctx, Fill *fill) {
    int loop = current;
    if (loop >= kLoopCount) return 0;
    // An open TLS connection can hold the engine for seconds (mbedtls then
    // keeps it for the whole digest); never wait for it.
    if (!tryLockEngines()) return 0;
    // Start from a clean peripheral, whatever used it last.
    periph_module_reset(PERIPH_SHA_MODULE);

    Ctx c;
    memcpy(c.hdr, job.words, sizeof(c.hdr));
    c.nonce = first;
    c.budget = count;
    c.run = 1;
    c.pad = 0x80000000u;
    bool useFill = fill && fill->ready && fillOk && loop == 0;
    Loop fn = useFill ? hwsha_classic_fill : kLoops[loop].fn;
    uint32_t q0 = 0;
    if (useFill) {
        fillLoad(c, *fill);
        q0 = c.q;
    }
    while (c.budget > 0 && fn(text(), &c)) {
        if (useFill) fillHit(c, *fill, limit, hit);
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
    if (useFill) {
        fillHit(c, *fill, limit, hit);
        fill->p = c.fillP;
        fill->q = c.q;
        fill->hashes += c.q - q0;
    }
    unlockEngines();
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
bool fillAvailable() { return false; }
void prepareFill(Fill &, const uint8_t *) {}
uint32_t scan(Job &, uint32_t, uint32_t, uint32_t, void (*)(uint32_t, const uint8_t *, void *), void *, Fill *) {
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
