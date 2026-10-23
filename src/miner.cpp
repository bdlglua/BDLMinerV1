#include "miner.h"

#include <Preferences.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "hw_sha.h"

MinerStats stats;

namespace {

// Hashes done between two looks at the job board. Sized for ~0.1-0.3 s of
// work so new jobs are picked up quickly while the overhead stays tiny.
constexpr uint32_t kBatchSw = 4096;
constexpr uint32_t kBatchHw = 16384;

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
    work::Work work;
    hwsha::Job hwJob;
};

Miner miners[MAX_MINERS];

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
        stats.blocksFound++;
        Serial.printf("\n!!!!! BLOCK FOUND !!!!! nonce=%08x job=%s\n", nonce, w.jobId.c_str());
    }
    if (d >= w.shareDifficulty || block) {
        stratum::Share s{};
        strlcpy(s.jobId, w.jobId.c_str(), sizeof(s.jobId));
        strlcpy(s.extranonce2, w.extranonce2Hex.c_str(), sizeof(s.extranonce2));
        s.ntime = w.ntime;
        s.nonce = nonce;
        s.difficulty = d;
        if (xQueueSend(shareQueue, &s, 0) == pdTRUE) stats.sharesFound++;
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

void minerTask(void *arg) {
    Miner &m = *(Miner *)arg;
    const uint32_t batch = m.hardware ? kBatchHw : kBatchSw;
    uint32_t seq = 0;
    uint32_t nonce = 0;
    bool haveWork = false;

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
        if (m.hardware) {
            hwsha::scan(m.hwJob, nonce, batch, limit, onCandidate, &m);
        } else {
            uint8_t hash[32];
            const sha::MidState &mid = m.work.mid;
            for (uint32_t n = nonce, end = nonce + batch; n != end; n++) {
                if (sha::scan(mid, n, limit, hash)) onCandidate(n, hash, &m);
            }
        }
        stats.hashes[m.index] += batch;
        nonce += batch;
        if (nonce == 0) haveWork = false;  // wrapped: 2^32 nonces exhausted

        // Let the idle task run (task watchdog) and same-priority tasks share.
        vTaskDelay(1);
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
        xTaskCreatePinnedToCore(minerTask, name, 6144, &m, 1, nullptr, core);
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
