#include "miner.h"

#include <Preferences.h>
#include <esp_rom_crc.h>
#include <stddef.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <atomic>

#include "core/mining_safety.h"
#include "hw_sha.h"
#include "log.h"
#include "ota.h"
#include "runtime.h"

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
std::atomic<bool> boardValid{false};
std::atomic<uint32_t> boardSeq{0};
std::atomic<uint32_t> workGeneration{0};  // only changes when earlier jobs become invalid
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
mining::SafetyLease safety;
std::atomic<bool> batteryLimited{false};

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
uint64_t sessionMs = 0;

// Lifetime totals as loaded at boot; this session is added on top.
uint64_t baseHashes = 0;
uint32_t baseShares = 0, baseBlocks = 0, baseSeconds = 0;
uint32_t savedBlocks = 0;

// Ring buffer of HISTORY_INTERVAL_S averages.
HistoryPoint history[HISTORY_LEN];
int historyLen = 0, historyHead = 0;
uint32_t historyRevision = 0;
uint64_t historyHashes = 0;
uint32_t historyMs = 0;
float temperatureSum = 0;
uint32_t temperatureSamples = 0, powerSum = 0, observationSamples = 0;
MinerProfile profiles[MAX_MINERS];

struct MiningWork : work::Work {
    uint32_t generation;
    uint32_t height;
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
    portENTER_CRITICAL(&statsMux);
    bool topCandidate = d > top[TOP_SHARES - 1].difficulty;
    portEXIT_CRITICAL(&statsMux);
    if (topCandidate) {
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
    // Bitcoin interprets the digest as little-endian. Inspect the exact bits
    // of fully verified candidates, avoiding floating-point boundary rounding.
    if (!(hash[24] | hash[25] | hash[26] | hash[27] | hash[28] | hash[29] | hash[30] | hash[31]))
        ++stats.over64bits;
    for (auto &b : blockBests) {
        if (b.height == w.height && w.height && d > b.best) b.best = d;
    }
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
    if ((w.exactShareTarget ? work::meetsTarget(hash, w.shareTarget) : d >= w.shareDifficulty) || block) {
        stratum::Share s{};
        strlcpy(s.jobId, w.jobId.c_str(), sizeof(s.jobId));
        strlcpy(s.extranonce2, w.extranonce2Hex.c_str(), sizeof(s.extranonce2));
        s.ntime = w.ntime;
        s.nonce = nonce;
        s.difficulty = d;
        s.version = uint32_t(w.header[0]) | uint32_t(w.header[1]) << 8 | uint32_t(w.header[2]) << 16 | uint32_t(w.header[3]) << 24;
        memcpy(s.hash, hash, 32);
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
        m.work.height = board.blockHeight();
        if (m.useFill) {
            work::build(board, extranonce2Counter++, m.work2);
            m.work2.generation = workGeneration;
            m.work2.height = m.work.height;
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
    if (useAsm && mining::fixedFilterSupports(limit)) {
        // For easier pool targets the fixed 16-bit filter would lose shares;
        // use the general C kernel instead.
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
            uint32_t started = millis();
            vTaskDelay(pdMS_TO_TICKS(100));
            portENTER_CRITICAL(&statsMux);
            profiles[m.index].waitingMs += uint32_t(millis() - started);
            portEXIT_CRITICAL(&statsMux);
            continue;
        }
        uint8_t power = minerPowerPercent();
        if (!power) {
            if (m.hardware) hwsha::idle();
            uint32_t started = millis();
            vTaskDelay(pdMS_TO_TICKS(200));
            portENTER_CRITICAL(&statsMux);
            profiles[m.index].pausedMs += uint32_t(millis() - started);
            portEXIT_CRITICAL(&statsMux);
            continue;
        }
        uint32_t batchStart = millis();
        uint32_t batchStartUs = micros();
        // New job, or the whole 32-bit nonce space of this work is done.
        if (!haveWork || seq != boardSeq) {
            haveWork = fetchWork(m, seq);
            nonce = 0;
            if (!haveWork) {
                if (m.hardware) hwsha::idle();
                continue;
            }
        }

        if (!minerPowerPercent()) continue;
        uint32_t limit = max(m.work.quickLimit, kBestDiffFilter);
        // Never run past the end of the nonce space (after software batches
        // `nonce` is only a multiple of kBatchSw).
        uint32_t left = 0u - nonce;
        uint32_t hwBatch = left && left < kBatchHw ? left : kBatchHw;
        uint32_t done = m.hardware ? hwsha::scan(m.hwJob, nonce, hwBatch, limit, onCandidate, &m.work,
                                                 m.useFill ? &m.fill : nullptr)
                                   : 0;
        if (m.useFill && m.fill.hashes) {
            portENTER_CRITICAL(&statsMux);
            stats.hashes[fillIndex] += m.fill.hashes;
            portEXIT_CRITICAL(&statsMux);
            m.fill.hashes = 0;
        }
        if (!done) {
            softwareBatch(m, nonce, kBatchSw, limit);
            done = kBatchSw;
        }
        portENTER_CRITICAL(&statsMux);
        stats.hashes[m.index] += done;
        profiles[m.index].hashingUs += uint32_t(micros() - batchStartUs);
        portEXIT_CRITICAL(&statsMux);
        nonce += done;
        if (nonce == 0) haveWork = false;  // wrapped: 2^32 nonces exhausted

        if (power < 100) {
            uint32_t restStarted = millis();
            if (m.hardware) hwsha::idle();
            uint32_t rest = mining::restMs(millis() - batchStart, power);
            // Release hardware before sleeping; revisit controls frequently.
            while (rest) {
                uint32_t part = min<uint32_t>(rest, 100);
                vTaskDelay(max<TickType_t>(1, pdMS_TO_TICKS(part)));
                rest -= part;
                if (minerPowerPercent() != power || seq != boardSeq || !boardValid) break;
            }
            lastPause = millis();
            portENTER_CRITICAL(&statsMux);
            profiles[m.index].restMs += uint32_t(millis() - restStarted);
            portEXIT_CRITICAL(&statsMux);
        }
        // Let the idle task and lower-priority tasks run.
        if (!m.ownsCore || millis() - lastPause >= kOwnCorePauseMs) {
            uint32_t restStarted = millis();
            vTaskDelay(1);
            portENTER_CRITICAL(&statsMux);
            profiles[m.index].restMs += uint32_t(millis() - restStarted);
            portEXIT_CRITICAL(&statsMux);
            lastPause = millis();
        }
    }
}

}  // namespace

void minerPublishJob(const work::Job &job, bool clean) {
    if (!jobMutex || !shareQueue) return;
    xSemaphoreTake(jobMutex, portMAX_DELAY);
    if (clean) {
        workGeneration++;
        xQueueReset(shareQueue);
    }
    board = job;
    double networkDiff = work::difficultyFromBits(job.nbits);
    portENTER_CRITICAL(&statsMux);
    stats.blockHeight = job.blockHeight();
    if (stats.blockHeight && stats.blockHeight != blockBests[blockHead].height) {
        blockHead = (blockHead + 1) % BLOCK_BESTS;
        blockBests[blockHead] = {stats.blockHeight, 0};
    }
    stats.networkDiff = networkDiff;
    stats.poolDiff = job.difficulty;
    portEXIT_CRITICAL(&statsMux);
    boardValid = true;
    boardSeq++;
    xSemaphoreGive(jobMutex);
}

void minerInvalidate() {
    if (!jobMutex || !shareQueue) return;
    xSemaphoreTake(jobMutex, portMAX_DELAY);
    boardValid = false;
    workGeneration++;
    xQueueReset(shareQueue);
    xSemaphoreGive(jobMutex);
}

bool minerNextShare(stratum::Share &share, uint32_t waitMs) {
    if (!shareQueue) return false;
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
    if (!runtime::started(jobMutex && shareQueue, runtime::Miner, "miner resources")) return;
    stats.startMillis = millis();
    lastTickMs = stats.startMillis;

    bool hw = hwsha::selfTest();
    // The classic ESP32 self-test can leave the engines reserved between
    // batches. Release them before task creation, including a failed start.
    hwsha::idle();
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
    bool canFill = hw && hwsha::fillAvailable() && tasks < MAX_MINERS;
    bool hardwareStarted = false;
    fillIndex = -1;
    stats.minerCount = 0;
    for (int core = cores - 1; core >= 0 && stats.minerCount < tasks; core--) {
        Miner &m = miners[stats.minerCount];
        m.index = stats.minerCount;
        m.hardware = hw && core == cores - 1;
        m.useFill = m.hardware && canFill;
        stats.engine[m.index] = m.hardware ? "hw" : "sw";
        char name[12];
        snprintf(name, sizeof(name), "miner%d", m.index);
        // On a single core the network needs the CPU more than any miner.
        m.ownsCore = m.hardware && cores > 1;
        if (!runtime::started(xTaskCreatePinnedToCore(minerTask, name, 6144, &m, m.ownsCore ? kHwMinerPriority : 1, nullptr, core) == pdPASS,
                              runtime::Miner, name)) continue;
        if (m.hardware) hardwareStarted = true;
        logf("[miner] %s engine=%s core=%d\n", name, stats.engine[m.index], core);
        stats.minerCount++;
    }
    if (hardwareStarted && canFill) {
        fillIndex = stats.minerCount;
        stats.engine[fillIndex] = "fill";
        stats.minerCount++;
        logf("[miner] engine=fill: a second work in the idle cycles of miner0\n");
    }
}

MinerStats minerStats() {
    portENTER_CRITICAL(&statsMux);
    MinerStats copy = stats;
    portEXIT_CRITICAL(&statsMux);
    return copy;
}

void minerPoolConnected(bool connected) {
    portENTER_CRITICAL(&statsMux);
    stats.poolConnected = connected;
    portEXIT_CRITICAL(&statsMux);
}

void minerPoolDifficulty(double difficulty) {
    portENTER_CRITICAL(&statsMux);
    stats.poolDiff = difficulty;
    portEXIT_CRITICAL(&statsMux);
}

void minerShareResult(bool accepted) {
    portENTER_CRITICAL(&statsMux);
    if (accepted) stats.accepted++;
    else stats.rejected++;
    portEXIT_CRITICAL(&statsMux);
}

uint32_t minerUptimeSeconds() {
    portENTER_CRITICAL(&statsMux);
    uint32_t value = sessionMs / 1000;
    portEXIT_CRITICAL(&statsMux);
    return value;
}

uint64_t minerTotalHashes() {
    portENTER_CRITICAL(&statsMux);
    uint64_t value = totalHashes;
    portEXIT_CRITICAL(&statsMux);
    return value;
}

double minerHashrate() {
    portENTER_CRITICAL(&statsMux);
    double value = hashrate;
    portEXIT_CRITICAL(&statsMux);
    return value;
}

double minerHashrateLive() {
    portENTER_CRITICAL(&statsMux);
    double value = hashrateLive;
    portEXIT_CRITICAL(&statsMux);
    return value;
}

double minerEngineRate(int i) {
    portENTER_CRITICAL(&statsMux);
    double value = i >= 0 && i < stats.minerCount ? engineRate[i] : 0;
    portEXIT_CRITICAL(&statsMux);
    return value;
}

void minerTick() {
    uint32_t now = millis();
    portENTER_CRITICAL(&statsMux);
    sessionMs += uint32_t(now - lastTickMs);
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
    stats.lifetimeSeconds = baseSeconds + sessionMs / 1000;

    if (!historyMs) {
        historyMs = now;
        historyHashes = totalHashes;
    } else if (now - historyMs >= HISTORY_INTERVAL_S * 1000UL) {
        HistoryPoint &point = history[historyHead];
        point.hashrate = (totalHashes - historyHashes) * 1000.0 / (now - historyMs);
        point.temperatureDeciC = temperatureSamples ? int16_t(lroundf(temperatureSum * 10 / temperatureSamples)) : INT16_MIN;
        point.powerPercent = observationSamples ? uint8_t(powerSum / observationSamples) : 0;
        point.coveragePercent = min<uint32_t>(100, uint64_t(observationSamples) * 100000 / (now - historyMs));
        temperatureSum = 0;
        temperatureSamples = powerSum = observationSamples = 0;
        historyHead = (historyHead + 1) % HISTORY_LEN;
        ++historyRevision;
        if (historyLen < HISTORY_LEN) historyLen++;
        historyMs = now;
        historyHashes = totalHashes;
    }

    // Flash wear: a found block is saved at once, a new best difficulty at
    // most once a minute, the lifetime totals every 10 minutes.
    static uint32_t lastSave = 0, retrySaveAt = 0;
    bool save = int32_t(now - retrySaveAt) >= 0 &&
                (stats.blocksFound != savedBlocks || (bestDirty && now - lastSave > 60000) || now - lastSave > 600000);
    portEXIT_CRITICAL(&statsMux);
    if (save) {
        if (minerSaveStats()) { lastSave = now; retrySaveAt = now; }
        else retrySaveAt = now + 30000;
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

void minerSetPower(uint8_t percent, bool batterySaver) {
    batteryLimited = batterySaver;
    safety.update(millis(), percent);
}

uint8_t minerPowerPercent() { return ota::busy() ? 0 : safety.power(millis()); }

Throttle minerThrottle() {
    uint8_t power = minerPowerPercent();
    return !power ? Throttle::Paused : power == 100 ? Throttle::Full
                               : batteryLimited ? Throttle::Half : Throttle::Reduced;
}

int minerHistory(float *out, int capacity) {
    portENTER_CRITICAL(&statsMux);
    int n = min(historyLen, capacity);
    for (int i = 0; i < n; i++) out[i] = history[(historyHead - n + i + HISTORY_LEN) % HISTORY_LEN].hashrate;
    portEXIT_CRITICAL(&statsMux);
    return n;
}

int minerHistoryPoints(HistoryPoint *out, int capacity, uint32_t *revision) {
    portENTER_CRITICAL(&statsMux);
    int n = min(historyLen, capacity);
    for (int i = 0; i < n; ++i) out[i] = history[(historyHead - n + i + HISTORY_LEN) % HISTORY_LEN];
    if (revision) *revision = historyRevision;
    portEXIT_CRITICAL(&statsMux);
    return n;
}

uint32_t minerHistoryRevision() {
    portENTER_CRITICAL(&statsMux);
    uint32_t revision = historyRevision;
    portEXIT_CRITICAL(&statsMux);
    return revision;
}

void minerObserve(float temperature, uint8_t effectivePower) {
    portENTER_CRITICAL(&statsMux);
    if (std::isfinite(temperature) && temperature > -100 && temperature < 200) {
        temperatureSum += temperature;
        ++temperatureSamples;
    }
    powerSum += effectivePower > 100 ? 100 : effectivePower;
    ++observationSamples;
    portEXIT_CRITICAL(&statsMux);
}

void minerProfiles(MinerProfile *out, int capacity) {
    portENTER_CRITICAL(&statsMux);
    for (int i = 0; i < min(capacity, MAX_MINERS); ++i) out[i] = profiles[i];
    portEXIT_CRITICAL(&statsMux);
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
SemaphoreHandle_t saveMutex = xSemaphoreCreateMutex();

uint32_t recordCrc(const StatsRecord &r) {
    return esp_rom_crc32_le(0, (const uint8_t *)&r, offsetof(StatsRecord, crc));
}

bool readSlot(Preferences &p, const char *key, StatsRecord &r) {
    if (p.getBytesLength(key) != sizeof(r)) return false;
    if (p.getBytes(key, &r, sizeof(r)) != sizeof(r)) return false;
    return r.magic == kStatsMagic && r.crc == recordCrc(r);
}

bool writeRecord(const StatsSnapshot &s) {
    StatsRecord r;
    memset(&r, 0, sizeof(r));
    r.magic = kStatsMagic;
    r.seq = statsSeq + 1;
    r.s = s;
    r.crc = recordCrc(r);
    Preferences p;
    if (!p.begin("stats", false)) return false;
    const char *key = r.seq & 1 ? "statB" : "statA";
    StatsRecord verified;
    bool ok = p.putBytes(key, &r, sizeof(r)) == sizeof(r) && readSlot(p, key, verified) && verified.seq == r.seq;
    p.end();
    if (!ok) logf("[stats] save failed; previous record kept\n");
    else statsSeq = r.seq;
    return ok;
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
    portENTER_CRITICAL(&statsMux);
    out.hashes = stats.lifetimeHashes;
    out.shares = stats.lifetimeShares;
    out.blocks = stats.lifetimeBlocks;
    out.seconds = stats.lifetimeSeconds;
    out.bestDiff = stats.bestDiff;
    memcpy(out.top, top, sizeof(out.top));
    portEXIT_CRITICAL(&statsMux);
}

bool minerSaveStats() {
    if (!saveMutex) return false;
    xSemaphoreTake(saveMutex, portMAX_DELAY);
    if (!statsLoaded || statsFrozen) {
        xSemaphoreGive(saveMutex);
        return true;
    }
    StatsSnapshot s;
    minerStatsSnapshot(s);
    bool ok = writeRecord(s);
    if (ok) {
        portENTER_CRITICAL(&statsMux);
        bestDirty = stats.bestDiff > s.bestDiff;
        savedBlocks = s.blocks - baseBlocks;
        portEXIT_CRITICAL(&statsMux);
    }
    xSemaphoreGive(saveMutex);
    return ok;
}

bool minerStatsRestore(const StatsSnapshot &s) {
    if (!saveMutex) return false;
    xSemaphoreTake(saveMutex, portMAX_DELAY);
    bool ok = writeRecord(s);
    if (ok) statsFrozen = true;
    xSemaphoreGive(saveMutex);
    return ok;
}
