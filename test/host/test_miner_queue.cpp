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
}
