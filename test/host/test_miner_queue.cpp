// Execute the real miner queue code with deterministic RTOS stubs. Delayed
// callbacks reproduce a batch finishing after a job change or disconnect.
#include <cassert>

#include "../../src/miner.cpp"

void logf(const char *, ...) {}
namespace hwsha {
void prepare(Job &, const uint8_t *) {}
void prepareFill(Fill &, const uint8_t *) {}
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
    delete shareQueue;
}
