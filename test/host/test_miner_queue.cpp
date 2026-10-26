// Execute the real miner queue code with deterministic RTOS stubs. Delayed
// callbacks reproduce a batch finishing after a job change or disconnect.
#include <cassert>

#define CONFIG_IDF_TARGET_ESP32 1
#include "../../src/miner.cpp"

int prefilterCalls = 0;
int hardwareIdleCalls = 0;
extern "C" bool sw_sha256d_prefilter(const uint32_t *, const uint8_t *, const uint32_t *) {
    ++prefilterCalls;
    return false;
}

void logf(const char *, ...) {}
namespace hwsha {
void idle() { ++hardwareIdleCalls; }
bool selfTest() { return true; }
bool available() { return true; }
bool fillAvailable() { return true; }
void prepare(Job &, const uint8_t *) {}
void prepareFill(Fill &, const uint8_t *) {}
uint32_t nonceAt(uint32_t position) { return __builtin_bswap32(position); }
}  // namespace hwsha

int main() {
    jobMutex = xSemaphoreCreateMutex();
    shareQueue = xQueueCreate(16, sizeof(QueuedShare));
    work::Job job;
    job.id = "reused-job-id";
    job.nbits = 0x1d00ffff;
    job.difficulty = 0.00001;
    Miner miner{};
    uint32_t seq = 0;
    stratum::Share result{};
    uint8_t hash[32] = {1};
    hash[29] = 1;  // meets the share target without being a block

    minerPublishJob(job, true);
    assert(fetchWork(miner, seq));
    MiningWork original = miner.work;
    onCandidate(1, hash, &original);
    assert(minerNextShare(result, 0) && result.nonce == 1);

    // A non-clean notification keeps previously issued work valid.
    minerPublishJob(job, false);
    onCandidate(2, hash, &original);
    assert(minerNextShare(result, 0) && result.nonce == 2);

    // An old batch may enqueue after the clean notification reset the queue.
    minerPublishJob(job, true);
    onCandidate(3, hash, &original);
    assert(!minerNextShare(result, 0));

    assert(fetchWork(miner, seq));
    MiningWork current = miner.work;
    onCandidate(4, hash, &original);
    onCandidate(5, hash, &current);
    assert(minerNextShare(result, 0) && result.nonce == 5);

    minerInvalidate();
    onCandidate(6, hash, &current);
    assert(!minerNextShare(result, 0));

    // The next pool session may reuse the same job ID and send clean=false.
    minerPublishJob(job, false);
    onCandidate(7, hash, &current);
    assert(!minerNextShare(result, 0));
    assert(fetchWork(miner, seq));
    onCandidate(8, hash, &miner.work);
    assert(minerNextShare(result, 0) && result.nonce == 8);

    puts("Miner queue: 7 checks passed");

    // A late result belongs to its work's height, even after a clean notify.
    job.coinb1.resize(46, 0);
    job.coinb1[42] = 3;
    job.coinb1[43] = 100;
    minerPublishJob(job, true);
    assert(fetchWork(miner, seq));
    original = miner.work;
    job.coinb1[43] = 101;
    minerPublishJob(job, true);
    onCandidate(9, hash, &original);
    BlockBest blocks[BLOCK_BESTS];
    assert(minerBlockBests(blocks, BLOCK_BESTS) == 2);
    assert(blocks[0].height == 101 && blocks[0].best == 0);
    assert(blocks[1].height == 100 && blocks[1].best > 0);
    assert(!minerNextShare(result, 0));

    // A known easy share outside the fixed 16-bit prefilter must reach C.
    assert(fetchWork(miner, seq));
    memset(miner.work.header, 0, sizeof(miner.work.header));
    memset(miner.work.networkTarget, 0, sizeof(miner.work.networkTarget));
    miner.work.shareDifficulty = 0.000001;
    sha::prepare(miner.work.mid, miner.work.header);
    useAsm = true;
    softwareBatch(miner, 18678, 1, work::quickLimitFor(0.000001));
    assert(prefilterCalls == 0);
    assert(minerNextShare(result, 0) && result.nonce == 18678);
    softwareBatch(miner, 18678, 1, 0xFFFF);
    assert(prefilterCalls == 1 && !minerNextShare(result, 0));

    // SV2's exact U256 target must distinguish hashes whose floating-point
    // difficulties are identical, and retain equality at the boundary.
    miner.work.exactShareTarget = true;
    memcpy(miner.work.shareTarget, hash, 32);
    miner.work.shareDifficulty = work::hashDifficulty(hash);
    uint8_t boundary[32]; memcpy(boundary, hash, 32); ++boundary[0];
    assert(work::hashDifficulty(boundary) == miner.work.shareDifficulty);
    onCandidate(41, boundary, &miner.work); assert(!minerNextShare(result, 0));
    onCandidate(42, hash, &miner.work); assert(minerNextShare(result, 0) && result.nonce == 42);
    assert(!memcmp(result.hash, hash, 32));
    --boundary[0]; --boundary[0];
    onCandidate(43, boundary, &miner.work); assert(minerNextShare(result, 0) && result.nonce == 43);
    miner.work.exactShareTarget = false;
    puts("SV2 miner targets: exact U256 boundary, rounded difficulty collision and full hash retained passed");

    // Uptime accumulates ticks instead of losing 49.7 days on wraparound.
    testMillis = stats.startMillis = lastTickMs = 1000;
    sessionMs = 0;
    baseSeconds = 1234;
    uint64_t elapsed = 0;
    for (int i = 0; i < 3; ++i) {
        testMillis += 0x70000000u;
        elapsed += 0x70000000u;
        minerTick();
        assert(minerUptimeSeconds() == elapsed / 1000);
        assert(minerStats().lifetimeSeconds == baseSeconds + elapsed / 1000);
    }

    minerSetPower(75);
    assert(minerPowerPercent() == 75);
    assert(ota::acquire(ota::Owner::Web));
    assert(minerPowerPercent() == 0);
    ota::release(ota::Owner::Web);
    assert(minerPowerPercent() == 75);
    testMillis += mining::kSafetyTimeoutMs + 1;
    assert(minerPowerPercent() == 0);
    puts("Miner records, easy shares, uptime wrap and safety integration: passed");
    delete shareQueue;
    // A hardware task that never started must not leave a phantom hardware
    // or fill engine in the public statistics. A surviving hardware task
    // still gets a contiguous fill slot if the software task fails.
    testTaskStarts = 0;
    testTaskFailAt = 0;
    minerStart();
    assert(hardwareIdleCalls > 0);
    assert(stats.minerCount == 1 && !strcmp(stats.engine[0], "sw") && fillIndex == -1);
    assert(runtime::failures().load() & runtime::Miner);
    delete shareQueue;
    testTaskStarts = 0;
    testTaskFailAt = 1;
    minerStart();
    assert(stats.minerCount == 2 && !strcmp(stats.engine[0], "hw") && !strcmp(stats.engine[1], "fill"));
    assert(fillIndex == 1);
    delete shareQueue;
    shareQueue = nullptr;
    jobMutex = 0;
    minerPublishJob(job, true);
    minerInvalidate();
    assert(!minerNextShare(result, 0));
    puts("Miner startup failures: no phantom engines, contiguous fill and safe recovery passed");
    // Exact 64-zero-bit boundary, including values indistinguishable as doubles.
    MiningWork probe{}; probe.shareDifficulty = 1e100;
    uint8_t rare[32] = {};
    memset(rare, 0xff, 24);  // largest integer with 64 leading zero bits
    stats.over64bits = 0;
    onCandidate(0, rare, &probe);
    assert(minerStats().over64bits == 1);
    for (int i = 24; i < 32; ++i) {
        rare[i] = 1;
        onCandidate(0, rare, &probe);
        assert(minerStats().over64bits == 1);
        rare[i] = 0;
    }
    memset(rare, 0, sizeof(rare)); rare[0] = 1;
    onCandidate(0, rare, &probe);
    assert(minerStats().over64bits == 2);
    puts("64-bit shares: exact threshold, byte order and rounded boundary passed");
    // Flash faults must not freeze a failed restore or discard dirty records.
    Preferences::data.clear(); statsLoaded = true; statsFrozen = false; statsSeq = 0;
    stats.lifetimeHashes = 123; stats.lifetimeBlocks = baseBlocks; stats.bestDiff = 2;
    bestDirty = true;
    assert(minerSaveStats() && statsSeq == 1);
    StatsSnapshot replacement{}; replacement.hashes = 777; replacement.bestDiff = 3;
    Preferences::failOpen = true; bestDirty = true;
    assert(!minerSaveStats() && bestDirty);
    Preferences::failOpen = false; Preferences::failWrite = true;
    assert(!minerStatsRestore(replacement) && !statsFrozen && statsSeq == 1);
    Preferences::failWrite = false;
    assert(minerStatsRestore(replacement) && statsFrozen && statsSeq == 2);
    stats = {}; minerLoadStats(); assert(baseHashes == 777);
    Preferences::data["statA"][offsetof(StatsRecord, s)] ^= 1;
    stats = {}; minerLoadStats(); assert(baseHashes == 123 && statsSeq == 1);
    puts("Statistics: storage failures preserve dirty state and old records; corrupt latest record falls back");
    // Hashrate, temperature and power share one ring and one revision.
    historyLen = historyHead = historyRevision = 0; historyMs = 1000; historyHashes = totalHashes = 0;
    temperatureSum = 0; temperatureSamples = observationSamples = powerSum = 0;
    statsLoaded = false; lastTickMs = 1000; stats = {}; stats.minerCount = 1; lastHashes[0] = 0;
    testMillis = 1000;
    for (int i = 0; i < 300; ++i) {
        minerObserve(i < 150 ? 40 : 60, i < 150 ? 100 : 50);
        stats.hashes[0] += 1000000; testMillis += 1000; minerTick();
    }
    HistoryPoint points[HISTORY_LEN]; uint32_t revision = 0;
    assert(minerHistoryPoints(points, HISTORY_LEN, &revision) == 1 && revision == 1);
    assert(points[0].hashrate == 1000000 && points[0].temperatureDeciC == 500);
    assert(points[0].powerPercent == 75 && points[0].coveragePercent == 100);
    for (int i = 0; i < 300; ++i) { minerObserve(NAN, 0); testMillis += 1000; minerTick(); }
    assert(minerHistoryPoints(points, HISTORY_LEN, &revision) == 2 && revision == 2);
    assert(points[1].temperatureDeciC == INT16_MIN && points[1].powerPercent == 0);
    for (int i = 0; i < HISTORY_LEN + 2; ++i) {
        minerObserve(50, 100); stats.hashes[0] += uint32_t(i + 1) * 300000;
        testMillis += 300000; minerTick();
    }
    assert(minerHistoryPoints(points, HISTORY_LEN, &revision) == HISTORY_LEN);
    assert(points[0].hashrate == 3000 && points[HISTORY_LEN - 1].hashrate == (HISTORY_LEN + 2) * 1000);
    puts("Aligned history: weighted hashrate, temperature/power samples, missing sensors, coverage and ring wrap passed");
}
