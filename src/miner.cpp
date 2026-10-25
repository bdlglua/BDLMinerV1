#include "miner.h"

#include <Preferences.h>
#include <esp_rom_crc.h>
#include <stddef.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "hw_sha.h"
#include "log.h"

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
volatile uint32_t workGeneration = 0;  // changes only when earlier jobs become invalid
uint64_t extranonce2Counter = 0;

struct QueuedShare {
    stratum::Share share;
    uint32_t generation;
};
QueueHandle_t shareQueue;
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
bool bestDirty = false;
BestShare top[TOP_SHARES];  // best first, difficulty 0 = empty
BlockBest blockBests[BLOCK_BESTS];  // ring, blockHead = the height mined now
int blockHead = 0;
volatile Throttle throttle = Throttle::Full;

uint32_t lastHashes[MAX_MINERS];
uint64_t totalHashes = 0;
// Hash rates are averages over the last 60 one-second ticks (like a pool's
// "1m" rate): the second or two the hardware loop pauses for a TLS handshake
// then costs a few percent for a minute instead of a dip to two thirds.
constexpr int kRateWindow = 60;
struct RateSample {
    uint32_t ms;
    uint32_t hashes[MAX_MINERS];
};
RateSample rateRing[kRateWindow];
int rateCount = 0, rateHead = 0;
double hashrate = 0, hashrateLive = 0;
constexpr int kLiveWindow = 10;
double engineRate[MAX_MINERS];
uint32_t lastTickMs = 0;

// Lifetime totals as loaded at boot; this session is added on top.
uint64_t baseHashes = 0;
uint32_t baseShares = 0, baseBlocks = 0, baseSeconds = 0;
uint32_t savedBlocks = 0;

// Ring buffer of HISTORY_INTERVAL_S averages.
float history[HISTORY_LEN];
int historyLen = 0, historyHead = 0;
uint64_t historyHashes = 0;
uint32_t historyMs = 0;

struct MiningWork : work::Work {
    uint32_t generation;
};

struct Miner {
    uint8_t index;
    bool hardware;
    bool ownsCore;  // alone on its core at kHwMinerPriority
    MiningWork work;
    hwsha::Job hwJob;
    // Hardware miner on the original ESP32: a second work hashed in software
    // in the idle cycles of the hardware loop (hwsha::Fill).
    bool useFill;
    MiningWork work2;
    hwsha::Fill fill;
};

Miner miners[MAX_MINERS];
int fillIndex = -1;  // stats.hashes slot of the idle-cycle hashing
bool useAsm = false;  // sw_sha256d_prefilter() passed its self-test

// `ctx` is the MiningWork the nonce belongs to.
void onCandidate(uint32_t nonce, const uint8_t hash[32], void *ctx) {
    const MiningWork &w = *(const MiningWork *)ctx;
    double d = work::hashDifficulty(hash);
    uint32_t when = 0;
    if (d > top[TOP_SHARES - 1].difficulty) {  // a peek, checked again below
        time_t now = time(nullptr);
        if (now > 1700000000) when = (uint32_t)now;  // clock set by NTP
    }

    portENTER_CRITICAL(&statsMux);
    if (d > stats.sessionBestDiff) stats.sessionBestDiff = d;
    if (d > stats.bestDiff) {
        stats.bestDiff = d;
        bestDirty = true;
    }
    if (d >= 1.0) stats.over32bits++;
    if (d > blockBests[blockHead].best) blockBests[blockHead].best = d;
    if (d > top[TOP_SHARES - 1].difficulty) {
        int i = TOP_SHARES - 1;
        for (; i > 0 && top[i - 1].difficulty < d; i--) top[i] = top[i - 1];
        top[i] = {d, when};  // saved with the lifetime totals (flash wear)
    }
    portEXIT_CRITICAL(&statsMux);

    bool block = work::meetsTarget(hash, w.networkTarget);
    if (block) {
        portENTER_CRITICAL(&statsMux);
        stats.blocksFound++;
        portEXIT_CRITICAL(&statsMux);
        logf("!!!!! BLOCK FOUND !!!!! nonce=%08x job=%s\n", nonce, w.jobId.c_str());
    }
    if (d >= w.shareDifficulty || block) {
        stratum::Share s{};
        strlcpy(s.jobId, w.jobId.c_str(), sizeof(s.jobId));
        strlcpy(s.extranonce2, w.extranonce2Hex.c_str(), sizeof(s.extranonce2));
        s.ntime = w.ntime;
        s.nonce = nonce;
        s.difficulty = d;
        QueuedShare queued{s, w.generation};
        if (xQueueSend(shareQueue, &queued, 0) == pdTRUE) {
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
        m.work.generation = workGeneration;
        if (m.useFill) {
            work::build(board, extranonce2Counter++, m.work2);
            m.work2.generation = workGeneration;
        }
    }
    xSemaphoreGive(jobMutex);
    if (ok && m.hardware) hwsha::prepare(m.hwJob, m.work.header);
    if (ok && m.useFill) {
        hwsha::prepareFill(m.fill, m.work2.header);
        m.fill.ctx = &m.work2;
    }
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
                onCandidate(n, hash, &m.work);
        }
        return;
    }
#endif
    for (uint32_t p = first, end = first + count; p != end; p++) {
        uint32_t n = m.hardware ? hwsha::nonceAt(p) : p;
        if (sha::scan(mid, n, limit, hash)) onCandidate(n, hash, &m.work);
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
            if (m.hardware) hwsha::idle();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        Throttle th = throttle;
        if (th == Throttle::Paused) {
            if (m.hardware) hwsha::idle();
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        uint32_t batchStart = millis();
        // New job, or the whole 32-bit nonce space of this work is done.
        if (!haveWork || seq != boardSeq) {
            haveWork = fetchWork(m, seq);
            nonce = 0;
            if (!haveWork) {
                if (m.hardware) hwsha::idle();
                continue;
            }
        }

        uint32_t limit = max(m.work.quickLimit, kBestDiffFilter);
        // Never run past the end of the nonce space (after software batches
        // `nonce` is only a multiple of kBatchSw).
        uint32_t left = 0u - nonce;
        uint32_t hwBatch = left && left < kBatchHw ? left : kBatchHw;
        uint32_t done = m.hardware ? hwsha::scan(m.hwJob, nonce, hwBatch, limit, onCandidate, &m.work,
                                                 m.useFill ? &m.fill : nullptr)
                                   : 0;
        if (m.useFill && m.fill.hashes) {
            stats.hashes[fillIndex] += m.fill.hashes;
            m.fill.hashes = 0;
        }
        if (!done) {
            softwareBatch(m, nonce, kBatchSw, limit);
            done = kBatchSw;
        }
        stats.hashes[m.index] += done;
        nonce += done;
        if (nonce == 0) haveWork = false;  // wrapped: 2^32 nonces exhausted

        if (th == Throttle::Half) {
            // As long idle as busy: about half the power.
            vTaskDelay(pdMS_TO_TICKS(max<uint32_t>(1, millis() - batchStart)));
            lastPause = millis();
        }
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
    if (clean) {
        workGeneration++;
        xQueueReset(shareQueue);
    }
    board = job;
    boardValid = true;
    boardSeq++;
    xSemaphoreGive(jobMutex);

    stats.blockHeight = job.blockHeight();
    if (stats.blockHeight && stats.blockHeight != blockBests[blockHead].height) {
        portENTER_CRITICAL(&statsMux);
        blockHead = (blockHead + 1) % BLOCK_BESTS;
        blockBests[blockHead] = {stats.blockHeight, 0};
        portEXIT_CRITICAL(&statsMux);
    }
    stats.networkDiff = work::difficultyFromBits(job.nbits);
    stats.poolDiff = job.difficulty;
}

void minerInvalidate() {
    xSemaphoreTake(jobMutex, portMAX_DELAY);
    boardValid = false;
    workGeneration++;
    xQueueReset(shareQueue);
    xSemaphoreGive(jobMutex);
}

bool minerNextShare(stratum::Share &share, uint32_t waitMs) {
    QueuedShare queued;
    TickType_t wait = pdMS_TO_TICKS(waitMs);
    while (xQueueReceive(shareQueue, &queued, wait) == pdTRUE) {
        // A batch already running during a clean job or disconnect can enqueue
        // its result after the queue reset. It must not reach the next session.
        if (queued.generation == workGeneration && boardValid) {
            share = queued.share;
            return true;
        }
        wait = 0;
    }
    return false;
}

void minerStart() {
    jobMutex = xSemaphoreCreateMutex();
    shareQueue = xQueueCreate(16, sizeof(QueuedShare));
    stats.startMillis = millis();

    bool hw = hwsha::selfTest();
    logf("[miner] hardware SHA: %s\n",
         hwsha::available() ? (hw ? "self-test OK" : "self-test FAILED, disabled") : "not supported");
#ifdef HAVE_SW_PREFILTER
    useAsm = prefilterSelfTest();
    logf("[miner] software SHA: %s\n", useAsm ? "assembly (self-test OK)" : "C (assembly self-test FAILED)");
#endif

    // One miner per core. With a hardware engine it gets its own core (the
    // last one, away from WiFi on dual-core chips) and software runs on the rest.
    int cores = portNUM_PROCESSORS;
    int tasks = min(cores, MAX_MINERS);
    if (hw && hwsha::fillAvailable() && tasks < MAX_MINERS) fillIndex = tasks;
    stats.minerCount = 0;
    for (int core = cores - 1; core >= 0 && stats.minerCount < tasks; core--) {
        Miner &m = miners[stats.minerCount];
        m.index = stats.minerCount;
        m.hardware = hw && core == cores - 1;
        m.useFill = m.hardware && fillIndex >= 0;
        stats.engine[m.index] = m.hardware ? "hw" : "sw";
        char name[12];
        snprintf(name, sizeof(name), "miner%d", m.index);
        // On a single core the network needs the CPU more than any miner.
        m.ownsCore = m.hardware && cores > 1;
        xTaskCreatePinnedToCore(minerTask, name, 6144, &m, m.ownsCore ? kHwMinerPriority : 1, nullptr, core);
        logf("[miner] %s engine=%s core=%d\n", name, stats.engine[m.index], core);
        stats.minerCount++;
    }
    if (fillIndex >= 0) {
        stats.engine[fillIndex] = "fill";
        stats.minerCount++;
        logf("[miner] engine=fill: a second work in the idle cycles of miner0\n");
    }
}

uint64_t minerTotalHashes() { return totalHashes; }

double minerHashrate() { return hashrate; }

double minerHashrateLive() { return hashrateLive; }

double minerEngineRate(int i) { return i >= 0 && i < stats.minerCount ? engineRate[i] : 0; }

void minerTick() {
    uint32_t now = millis();
    uint32_t delta = 0, engineDelta[MAX_MINERS];
    for (int i = 0; i < stats.minerCount; i++) {
        uint32_t h = stats.hashes[i];
        engineDelta[i] = h - lastHashes[i];
        delta += engineDelta[i];
        lastHashes[i] = h;
    }
    totalHashes += delta;
    if (lastTickMs) {
        RateSample &r = rateRing[rateHead];
        r.ms = now - lastTickMs;
        for (int i = 0; i < MAX_MINERS; i++) r.hashes[i] = i < stats.minerCount ? engineDelta[i] : 0;
        rateHead = (rateHead + 1) % kRateWindow;
        if (rateCount < kRateWindow) rateCount++;
        uint32_t ms = 0;
        uint64_t sum[MAX_MINERS] = {0};
        for (int k = 0; k < rateCount; k++) {
            ms += rateRing[k].ms;
            for (int i = 0; i < MAX_MINERS; i++) sum[i] += rateRing[k].hashes[i];
        }
        if (ms) {
            double total = 0;
            for (int i = 0; i < MAX_MINERS; i++) {
                engineRate[i] = sum[i] * 1000.0 / ms;
                total += engineRate[i];
            }
            hashrate = total;
        }
        uint32_t liveMs = 0;
        uint64_t liveSum = 0;
        for (int k = 1; k <= min(rateCount, kLiveWindow); k++) {
            const RateSample &s = rateRing[(rateHead - k + kRateWindow) % kRateWindow];
            liveMs += s.ms;
            for (int i = 0; i < MAX_MINERS; i++) liveSum += s.hashes[i];
        }
        if (liveMs) hashrateLive = liveSum * 1000.0 / liveMs;
    }
    lastTickMs = now;

    stats.lifetimeHashes = baseHashes + totalHashes;
    stats.lifetimeShares = baseShares + stats.accepted;
    stats.lifetimeBlocks = baseBlocks + stats.blocksFound;
    stats.lifetimeSeconds = baseSeconds + (now - stats.startMillis) / 1000;

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

int minerBlockBests(BlockBest *out, int capacity) {
    int n = 0;
    portENTER_CRITICAL(&statsMux);
    for (int i = 0; i < BLOCK_BESTS && n < capacity; i++) {
        const BlockBest &b = blockBests[(blockHead - i + BLOCK_BESTS) % BLOCK_BESTS];
        if (b.height) out[n++] = b;
    }
    portEXIT_CRITICAL(&statsMux);
    return n;
}

int minerTopShares(BestShare *out, int capacity) {
    int n = 0;
    portENTER_CRITICAL(&statsMux);
    for (int i = 0; i < TOP_SHARES && n < capacity && top[i].difficulty > 0; i++) out[n++] = top[i];
    portEXIT_CRITICAL(&statsMux);
    return n;
}

void minerSetThrottle(Throttle t) { throttle = t; }

Throttle minerThrottle() { return throttle; }

int minerHistory(float *out, int capacity) {
    int n = min(historyLen, capacity);
    for (int i = 0; i < n; i++) out[i] = history[(historyHead - n + i + HISTORY_LEN) % HISTORY_LEN];
    return n;
}

namespace {

// One saved record, written in turn to the slots "statA" and "statB" (the
// scheme of NerdMiner Modded): at boot the newest slot with a good CRC wins.
constexpr uint32_t kStatsMagic = 0x314C4442;  // "BDL1"
struct StatsRecord {
    uint32_t magic;
    uint32_t seq;
    StatsSnapshot s;
    uint32_t crc;  // over every byte before it
};

uint32_t statsSeq = 0;
bool statsLoaded = false;  // never save before the saved stats were read back
bool statsFrozen = false;  // a restored backup waits for the restart

uint32_t recordCrc(const StatsRecord &r) {
    return esp_rom_crc32_le(0, (const uint8_t *)&r, offsetof(StatsRecord, crc));
}

bool readSlot(Preferences &p, const char *key, StatsRecord &r) {
    if (p.getBytesLength(key) != sizeof(r)) return false;
    p.getBytes(key, &r, sizeof(r));
    return r.magic == kStatsMagic && r.crc == recordCrc(r);
}

void writeRecord(const StatsSnapshot &s) {
    StatsRecord r;
    memset(&r, 0, sizeof(r));
    r.magic = kStatsMagic;
    r.seq = ++statsSeq;
    r.s = s;
    r.crc = recordCrc(r);
    Preferences p;
    p.begin("stats", false);
    if (p.putBytes(r.seq & 1 ? "statB" : "statA", &r, sizeof(r)) != sizeof(r)) logf("[stats] save failed\n");
    p.end();
}

}  // namespace

void minerLoadStats() {
    Preferences p;
    p.begin("stats", true);
    StatsRecord a, b;
    bool okA = readSlot(p, "statA", a), okB = readSlot(p, "statB", b);
    StatsSnapshot s{};
    if (okA || okB) {
        const StatsRecord &r = okA && (!okB || (int32_t)(a.seq - b.seq) > 0) ? a : b;
        s = r.s;
        statsSeq = r.seq;
        logf("[stats] restored save #%u%s\n", (unsigned)r.seq, okA && okB ? "" : " (one slot only)");
    } else if (p.isKey("hashes")) {
        // The separate keys of earlier versions, read once.
        s.bestDiff = p.getDouble("best", 0);
        s.hashes = p.getULong64("hashes", 0);
        s.shares = p.getULong("shares", 0);
        s.blocks = p.getULong("blocks", 0);
        s.seconds = p.getULong("seconds", 0);
        p.getBytes("top", s.top, sizeof(s.top));
        logf("[stats] restored from the previous format\n");
    }
    p.end();

    stats.bestDiff = s.bestDiff;
    baseHashes = s.hashes;
    baseShares = s.shares;
    baseBlocks = s.blocks;
    baseSeconds = s.seconds;
    memcpy(top, s.top, sizeof(top));
    stats.lifetimeHashes = baseHashes;
    stats.lifetimeShares = baseShares;
    stats.lifetimeBlocks = baseBlocks;
    stats.lifetimeSeconds = baseSeconds;
    statsLoaded = true;
}

void minerStatsSnapshot(StatsSnapshot &out) {
    memset(&out, 0, sizeof(out));
    out.hashes = stats.lifetimeHashes;
    out.shares = stats.lifetimeShares;
    out.blocks = stats.lifetimeBlocks;
    out.seconds = stats.lifetimeSeconds;
    portENTER_CRITICAL(&statsMux);
    out.bestDiff = stats.bestDiff;
    memcpy(out.top, top, sizeof(out.top));
    portEXIT_CRITICAL(&statsMux);
}

void minerSaveStats() {
    if (!statsLoaded || statsFrozen) return;
    bestDirty = false;
    savedBlocks = stats.blocksFound;
    StatsSnapshot s;
    minerStatsSnapshot(s);
    writeRecord(s);
}

void minerStatsRestore(const StatsSnapshot &s) {
    writeRecord(s);
    statsFrozen = true;
}
