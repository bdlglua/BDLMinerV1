#include "miner.h"

#include <Preferences.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "hw_sha.h"

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3
// Hand-scheduled assembly of sha::scan() up to its early exit, in IRAM
// (sw_sha_prefilter.S): true exactly when the top 16 bits of the hash are
// zero. ~5300 instructions against ~7250 for the compiled C kernel.
extern "C" bool sw_sha256d_prefilter(const uint32_t *mid, const uint8_t *tail, const uint32_t *bake);
#define HAVE_SW_PREFILTER 1
#endif

MinerStats stats;

namespace {

// Hashes done between two looks at the job board (and two updates of the
// hash counter): ~0.1 s of software work, ~67 ms of the original ESP32's
// hardware loop. Both divide 2^32, so a work's nonce space always ends on a
// batch boundary.
constexpr uint32_t kBatchSw = 4096;
constexpr uint32_t kBatchHw = 65536;

// The hardware miner owns the last core of dual-core chips: above every task
// that could share it (everything else, Arduino loop included, runs on core
// 0). Stealing its CPU leaves the SHA engine idle, so it only pauses every
// 10 s, for the idle task; miners sharing a core pause after every batch.
constexpr UBaseType_t kHwMinerPriority = 19;
constexpr uint32_t kOwnCorePauseMs = 10000;

// Candidates under this top-32-bit value (diff >= ~1.5e-5) are fully
// evaluated, which is what feeds the "best difficulty" statistic.
constexpr uint32_t kBestDiffFilter = 0x0000FFFF;

SemaphoreHandle_t jobMutex;
work::Job board;
volatile bool boardValid = false;
volatile uint32_t boardSeq = 0;
uint64_t extranonce2Counter = 0;

QueueHandle_t shareQueue;
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
bool bestDirty = false;

uint32_t lastHashes[MAX_MINERS];
uint64_t totalHashes = 0;
double hashrate = 0;
uint32_t lastTickMs = 0;

// Lifetime totals as loaded at boot; this session is added on top.
uint64_t baseHashes = 0;
uint32_t baseShares = 0, baseBlocks = 0;
uint32_t savedBlocks = 0;

// Ring buffer of HISTORY_INTERVAL_S averages.
float history[HISTORY_LEN];
int historyLen = 0, historyHead = 0;
uint64_t historyHashes = 0;
uint32_t historyMs = 0;

struct Miner {
    uint8_t index;
    bool hardware;
    bool ownsCore;  // alone on its core at kHwMinerPriority
    work::Work work;
    hwsha::Job hwJob;
};

Miner miners[MAX_MINERS];
bool useAsm = false;  // sw_sha256d_prefilter() passed its self-test

void onCandidate(uint32_t nonce, const uint8_t hash[32], void *ctx) {
    const work::Work &w = ((Miner *)ctx)->work;
    double d = work::hashDifficulty(hash);

    portENTER_CRITICAL(&statsMux);
    if (d > stats.sessionBestDiff) stats.sessionBestDiff = d;
    if (d > stats.bestDiff) {
        stats.bestDiff = d;
        bestDirty = true;
    }
    if (d >= 1.0) stats.over32bits++;
    portEXIT_CRITICAL(&statsMux);

    bool block = work::meetsTarget(hash, w.networkTarget);
    if (block) {
        portENTER_CRITICAL(&statsMux);
        stats.blocksFound++;
        portEXIT_CRITICAL(&statsMux);
        Serial.printf("\n!!!!! BLOCK FOUND !!!!! nonce=%08x job=%s\n", nonce, w.jobId.c_str());
    }
    if (d >= w.shareDifficulty || block) {
        stratum::Share s{};
        strlcpy(s.jobId, w.jobId.c_str(), sizeof(s.jobId));
        strlcpy(s.extranonce2, w.extranonce2Hex.c_str(), sizeof(s.extranonce2));
        s.ntime = w.ntime;
        s.nonce = nonce;
        s.difficulty = d;
        if (xQueueSend(shareQueue, &s, 0) == pdTRUE) {
            portENTER_CRITICAL(&statsMux);
            stats.sharesFound++;
            portEXIT_CRITICAL(&statsMux);
        }
    }
}

// Takes the latest job from the board and builds fresh work with a unique
// extranonce2. Returns false if there is no job.
bool fetchWork(Miner &m, uint32_t &seq) {
    xSemaphoreTake(jobMutex, portMAX_DELAY);
    bool ok = boardValid;
    if (ok) {
        seq = boardSeq;
        work::build(board, extranonce2Counter++, m.work);
    }
    xSemaphoreGive(jobMutex);
    if (ok && m.hardware) hwsha::prepare(m.hwJob, m.work.header);
    return ok;
}

// Software kernel over positions [first, first + count). A hardware miner
// whose engine is busy (TLS) keeps to the engine's nonce order so that no
// nonce is hashed twice.
void softwareBatch(Miner &m, uint32_t first, uint32_t count, uint32_t limit) {
    uint8_t hash[32];
    const sha::MidState &mid = m.work.mid;
#ifdef HAVE_SW_PREFILTER
    if (useAsm) {
        // The assembly only finds hashes with top-32 bits <= 0xFFFF, which
        // `limit` (>= kBestDiffFilter) always covers; sha::scan() then makes
        // the hash of that 1 nonce in 65536 and applies `limit`.
        uint8_t tail[16];
        memcpy(tail, m.work.header + 64, 16);
        for (uint32_t p = first, end = first + count; p != end; p++) {
            uint32_t n = m.hardware ? hwsha::nonceAt(p) : p;
            memcpy(tail + 12, &n, 4);  // little-endian, as in the header
            if (sw_sha256d_prefilter(mid.mid, tail, mid.bake) && sha::scan(mid, n, limit, hash))
                onCandidate(n, hash, &m);
        }
        return;
    }
#endif
    for (uint32_t p = first, end = first + count; p != end; p++) {
        uint32_t n = m.hardware ? hwsha::nonceAt(p) : p;
        if (sha::scan(mid, n, limit, hash)) onCandidate(n, hash, &m);
    }
}

#ifdef HAVE_SW_PREFILTER
// Compares the assembly with sha::scan() on 6 known candidates (the header and
// nonces of hwsha's known-answer test) and the 512 nonces after each.
bool prefilterSelfTest() {
    static const uint32_t kCandidates[] = {0xa94c, 0xe496, 0x1f476, 0x302f0, 0x35af9, 0x3cf5c};
    uint8_t header[80] = {0};
    for (int i = 0; i < 19; i++) {
        uint32_t w = 0x9E3779B9u * (i + 1);
        header[4 * i] = w >> 24;
        header[4 * i + 1] = w >> 16;
        header[4 * i + 2] = w >> 8;
        header[4 * i + 3] = w;
    }
    sha::MidState ms;
    sha::prepare(ms, header);
    uint8_t tail[16], hash[32];
    memcpy(tail, header + 64, 16);
    int hits = 0;
    for (uint32_t c : kCandidates) {
        uint32_t first = __builtin_bswap32(0x1000 + c);
        for (uint32_t n = first; n != first + 513; n++) {
            memcpy(tail + 12, &n, 4);
            bool fast = sw_sha256d_prefilter(ms.mid, tail, ms.bake);
            if (fast != sha::scan(ms, n, 0x0000FFFF, hash)) return false;
            hits += fast;
        }
    }
    return hits >= 6;
}
#endif

void minerTask(void *arg) {
    Miner &m = *(Miner *)arg;
    uint32_t seq = 0;
    uint32_t nonce = 0;
    bool haveWork = false;
    uint32_t lastPause = millis();

    for (;;) {
        if (!boardValid) {
            haveWork = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        // New job, or the whole 32-bit nonce space of this work is done.
        if (!haveWork || seq != boardSeq) {
            haveWork = fetchWork(m, seq);
            nonce = 0;
            if (!haveWork) continue;
        }

        uint32_t limit = max(m.work.quickLimit, kBestDiffFilter);
        // Never run past the end of the nonce space (after software batches
        // `nonce` is only a multiple of kBatchSw).
        uint32_t left = 0u - nonce;
        uint32_t hwBatch = left && left < kBatchHw ? left : kBatchHw;
        uint32_t done = m.hardware ? hwsha::scan(m.hwJob, nonce, hwBatch, limit, onCandidate, &m) : 0;
        if (!done) {
            softwareBatch(m, nonce, kBatchSw, limit);
            done = kBatchSw;
        }
        stats.hashes[m.index] += done;
        nonce += done;
        if (nonce == 0) haveWork = false;  // wrapped: 2^32 nonces exhausted

        // Let the idle task and lower-priority tasks run.
        if (!m.ownsCore || millis() - lastPause >= kOwnCorePauseMs) {
            vTaskDelay(1);
            lastPause = millis();
        }
    }
}

}  // namespace

void minerPublishJob(const work::Job &job, bool clean) {
    xSemaphoreTake(jobMutex, portMAX_DELAY);
    board = job;
    boardValid = true;
    boardSeq++;
    xSemaphoreGive(jobMutex);

    stats.blockHeight = job.blockHeight();
    stats.networkDiff = work::difficultyFromBits(job.nbits);
    stats.poolDiff = job.difficulty;
    if (clean) xQueueReset(shareQueue);  // shares for stale jobs would be rejected
}

void minerInvalidate() {
    boardValid = false;
    xQueueReset(shareQueue);
}

bool minerNextShare(stratum::Share &share, uint32_t waitMs) {
    return xQueueReceive(shareQueue, &share, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void minerStart() {
    jobMutex = xSemaphoreCreateMutex();
    shareQueue = xQueueCreate(16, sizeof(stratum::Share));
    stats.startMillis = millis();

    bool hw = hwsha::selfTest();
    Serial.printf("[miner] hardware SHA: %s\n",
                  hwsha::available() ? (hw ? "self-test OK" : "self-test FAILED, disabled") : "not supported");
#ifdef HAVE_SW_PREFILTER
    useAsm = prefilterSelfTest();
    Serial.printf("[miner] software SHA: %s\n", useAsm ? "assembly (self-test OK)" : "C (assembly self-test FAILED)");
#endif

    // One miner per core. With a hardware engine it gets its own core (the
    // last one, away from WiFi on dual-core chips) and software runs on the rest.
    int cores = portNUM_PROCESSORS;
    stats.minerCount = 0;
    for (int core = cores - 1; core >= 0 && stats.minerCount < MAX_MINERS; core--) {
        Miner &m = miners[stats.minerCount];
        m.index = stats.minerCount;
        m.hardware = hw && core == cores - 1;
        stats.engine[m.index] = m.hardware ? "hw" : "sw";
        char name[12];
        snprintf(name, sizeof(name), "miner%d", m.index);
        // On a single core the network needs the CPU more than any miner.
        m.ownsCore = m.hardware && cores > 1;
        xTaskCreatePinnedToCore(minerTask, name, 6144, &m, m.ownsCore ? kHwMinerPriority : 1, nullptr, core);
        Serial.printf("[miner] %s engine=%s core=%d\n", name, stats.engine[m.index], core);
        stats.minerCount++;
    }
}

uint64_t minerTotalHashes() { return totalHashes; }

double minerHashrate() { return hashrate; }

void minerTick() {
    uint32_t now = millis();
    uint32_t delta = 0;
    for (int i = 0; i < stats.minerCount; i++) {
        uint32_t h = stats.hashes[i];
        delta += h - lastHashes[i];
        lastHashes[i] = h;
    }
    totalHashes += delta;
    if (lastTickMs) {
        double dt = (now - lastTickMs) / 1000.0;
        if (dt > 0) {
            double inst = delta / dt;
            hashrate = hashrate == 0 ? inst : hashrate * 0.8 + inst * 0.2;
        }
    }
    lastTickMs = now;

    stats.lifetimeHashes = baseHashes + totalHashes;
    stats.lifetimeShares = baseShares + stats.accepted;
    stats.lifetimeBlocks = baseBlocks + stats.blocksFound;

    if (!historyMs) {
        historyMs = now;
        historyHashes = totalHashes;
    } else if (now - historyMs >= HISTORY_INTERVAL_S * 1000UL) {
        history[historyHead] = (totalHashes - historyHashes) * 1000.0 / (now - historyMs);
        historyHead = (historyHead + 1) % HISTORY_LEN;
        if (historyLen < HISTORY_LEN) historyLen++;
        historyMs = now;
        historyHashes = totalHashes;
    }

    // Flash wear: a found block is saved at once, a new best difficulty at
    // most once a minute, the lifetime totals every 10 minutes.
    static uint32_t lastSave = 0;
    if (stats.blocksFound != savedBlocks || (bestDirty && now - lastSave > 60000) || now - lastSave > 600000) {
        lastSave = now;
        minerSaveStats();
    }
}

int minerHistory(float *out, int capacity) {
    int n = min(historyLen, capacity);
    for (int i = 0; i < n; i++) out[i] = history[(historyHead - n + i + HISTORY_LEN) % HISTORY_LEN];
    return n;
}

void minerLoadStats() {
    Preferences p;
    p.begin("stats", true);
    stats.bestDiff = p.getDouble("best", 0);
    baseHashes = p.getULong64("hashes", 0);
    baseShares = p.getULong("shares", 0);
    baseBlocks = p.getULong("blocks", 0);
    p.end();
    stats.lifetimeHashes = baseHashes;
    stats.lifetimeShares = baseShares;
    stats.lifetimeBlocks = baseBlocks;
}

void minerSaveStats() {
    bestDirty = false;
    savedBlocks = stats.blocksFound;
    Preferences p;
    p.begin("stats", false);
    p.putDouble("best", stats.bestDiff);
    p.putULong64("hashes", stats.lifetimeHashes);
    p.putULong("shares", stats.lifetimeShares);
    p.putULong("blocks", stats.lifetimeBlocks);
    p.end();
}
