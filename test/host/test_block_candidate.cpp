// Exercise the production candidate path offline, including a known Bitcoin
// proof and independently hashed easy headers. Never contact a real pool.
#define CONFIG_IDF_TARGET_ESP32 1
#include "../../src/miner.cpp"
#include <cassert>
void logf(const char *, ...) {}
extern "C" bool sw_sha256d_prefilter(const uint32_t *, const uint8_t *, const uint32_t *) { return false; }
namespace hwsha {
void idle() {} bool selfTest() { return false; } bool available() { return false; } bool fillAvailable() { return false; }
void prepare(Job &, const uint8_t *) {} void prepareFill(Fill &, const uint8_t *) {}
uint32_t nonceAt(uint32_t n) { return n; }
}

int main() {
    jobMutex = xSemaphoreCreateMutex();
    shareQueue = xQueueCreate(16, sizeof(QueuedShare));
    work::Job job;
    job.id = "proof"; job.nbits = 0x1d00ffff; job.difficulty = 1e-6;
    job.coinb1.resize(46, 0); job.coinb1[42] = 3; job.coinb1[43] = 100;
    minerPublishJob(job, true);
    Miner miner{}; uint32_t seq = 0; assert(fetchWork(miner, seq));
    MiningWork w = miner.work;
    const char *genesis = "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c";
    assert(work::fromHex(genesis, w.header, 80));
    uint8_t proof[32]; sha::sha256d(w.header, 80, proof);
    uint8_t id[32]; for (int i = 0; i < 32; ++i) id[i] = proof[31 - i];
    assert(work::toHex(id, 32) == "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
    uint8_t ordinary[32] = {1}; ordinary[29] = 1;
    for (unsigned i = 0; i < 16; ++i) onCandidate(i, ordinary, &w);
    assert(shareQueue->entries.size() == 16);
    onCandidate(2083236893, proof, &w);
    assert(stats.blocksFound == 1 && stats.sharesFound == 17);
    StatsSnapshot snapshot; minerStatsSnapshot(snapshot);
    assert(snapshot.blocks == 1); // survives a save before the next minerTick
    onCandidate(2083236893, proof, &w);
    assert(stats.blocksFound == 1 && blockQueueCount == 1);
    job.coinb1[43] = 101; minerPublishJob(job, false);
    BlockCandidate last{};
    assert(minerLastBlockCandidate(last) && last.height == 100 && last.state == BlockQueued);
    assert(stats.blockHeight == 101 && !memcmp(last.hash, proof, 32));
    stratum::Share share;
    assert(minerNextShare(share, 0) && share.nonce == 2083236893);
    assert(shareQueue->entries.size() == 16); // priority precedes all ordinary work
    minerPoolConnected(true);
    minerShareSent(share.difficulty, false, 7, share.hash);
    assert(minerLastBlockCandidate(last) && last.state == BlockSubmitted);
    for (unsigned i = 0; i < 25; ++i) minerShareSent(1, false, 100 + i);
    minerShareResult(false, {106, 1});
    assert(minerLastBlockCandidate(last) && last.state == BlockSubmitted);
    minerShareResult(true, {7, 1});
    assert(minerLastBlockCandidate(last) && last.state == BlockShareAccepted);
    minerInvalidate();
    assert(minerLastBlockCandidate(last) && last.state == BlockShareAccepted);
    onCandidate(2083236893, proof, &w);
    assert(stats.blocksFound == 1 && !minerNextShare(share, 0));
    minerPublishJob(job, true);
    onCandidate(2083236893, proof, &w);
    assert(stats.blocksFound == 1 && !minerNextShare(share, 0));

    // Produce distinct, real SHA-256d proofs under an easy synthetic target.
    assert(fetchWork(miner, seq)); w = miner.work;
    assert(work::targetFromBits(0x207fffff, w.networkTarget));
    auto candidate = [&](uint32_t first) {
        uint8_t hash[32];
        for (uint32_t nonce = first;; ++nonce) {
            for (int i = 0; i < 4; ++i) w.header[76 + i] = nonce >> (8 * i);
            sha::sha256d(w.header, 80, hash);
            if (work::meetsTarget(hash, w.networkTarget)) { onCandidate(nonce, hash, &w); break; }
        }
    };
    candidate(1000); assert(minerNextShare(share, 0));
    minerShareSent(share.difficulty, false, 8, share.hash);
    minerShareResult(false, {8, 1});
    assert(minerLastBlockCandidate(last) && last.state == BlockShareRejected);
    candidate(2000); assert(minerNextShare(share, 0));
    minerShareSent(share.difficulty, false, 9, share.hash);
    minerPoolConnected(false);
    assert(minerLastBlockCandidate(last) && last.state == BlockUnanswered);
    // Reused submission IDs in a new connection cannot settle an old result.
    minerShareSent(1, false, 9); minerShareResult(true, {9, 1});
    assert(minerLastBlockCandidate(last) && last.state == BlockUnanswered);
    candidate(3000); minerPublishJob(job, true);
    assert(minerLastBlockCandidate(last) && last.state == BlockStale);
    assert(!minerNextShare(share, 0));
    unsigned count = stats.blocksFound;
    candidate(4000); // work from before the clean job: no new block alert/count
    assert(stats.blocksFound == count);
    assert(fetchWork(miner, seq)); w = miner.work;
    assert(work::targetFromBits(0x207fffff, w.networkTarget));
    candidate(5000); candidate(6000);
    assert(blockQueueCount == 2);
    candidate(7000);
    assert(minerLastBlockCandidate(last) && last.state == BlockQueueFull);
    assert(blockQueueCount == 2 && stats.blocksFound == count + 3);
    assert(minerNextShare(share, 0) && minerNextShare(share, 0));
    assert(!minerNextShare(share, 0));
    minerStatsSnapshot(snapshot); assert(snapshot.blocks == stats.blocksFound);
    // The extension must keep old statA/statB records readable while saving
    // proof metadata in two independently verified CRC slots.
    statsLoaded = true;
    assert(minerSaveStats() && candidateSaveSeq == 1);
    const auto oldStatsSize = sizeof(StatsRecord);
    assert(Preferences::data["statB"].size() == oldStatsSize);
    uint8_t oldProof[32]; memcpy(oldProof, last.hash, 32);
    candidate(8000); assert(minerNextShare(share, 0));
    minerShareSent(share.difficulty, false, 200, share.hash);
    Preferences::failWrite = true;
    assert(!minerSaveStats() && candidateRevision != savedCandidateRevision && candidateSaveSeq == 1);
    Preferences::failWrite = false;
    assert(minerSaveStats() && candidateSaveSeq == 2);
    auto reboot = [&]() {
        candidateHead = candidateCount = blockQueueCount = 0;
        candidateRevision = savedCandidateRevision = candidateSaveSeq = 0;
        havePreviousCandidate = false; previousCandidate = {}; stats = {};
        minerLoadStats();
    };
    reboot();
    assert(minerLastBlockCandidate(last) && last.previousSession && last.number == 0);
    assert(last.state == BlockUnanswered && !memcmp(last.hash, share.hash, 32));
    assert(!minerNextShare(share, 0)); // never replay a saved proof
    Preferences::data["blockA"][offsetof(CandidateSave, hash)] ^= 1;
    reboot();
    assert(minerLastBlockCandidate(last) && last.state == BlockQueueFull && !memcmp(last.hash, oldProof, 32));
    // A corrupted/truncated extension must not erase ordinary lifetime stats.
    Preferences::data["blockB"].resize(3);
    reboot();
    assert(!minerLastBlockCandidate(last) && stats.lifetimeBlocks > 0);
    for(unsigned id=coin::BCH;id<coin::Count;++id) {
    activeMiningCoin = id;
    assert(fetchWork(miner, seq)); w = miner.work;
    assert(work::targetFromBits(0x207fffff, w.networkTarget));
    candidate(9000 + id); assert(minerLastBlockCandidate(last) && last.coin == id);
    assert(minerSaveStats());
    CandidateSave saved{}; Preferences prefs;
    assert(readCandidateSlot(prefs, candidateSaveSeq & 1 ? "blockB" : "blockA", saved));
    assert(saved.magic == kMultiCoinCandidateMagic && saved.coin == id);
    reboot(); assert(minerLastBlockCandidate(last) && last.previousSession && last.coin == id);
    }
    // The software engine and dashboard use the same Peercoin height offset.
    activeMiningCoin = coin::PPC;
    job.coinb1.assign(51,0);job.coinb1[0]=2;job.coinb1[46]=3;
    job.coinb1[47]=0xc8;job.coinb1[48]=0xb8;job.coinb1[49]=0x0d;
    minerPublishJob(job,true);assert(fetchWork(miner,seq));
    assert(miner.work.height==899272 && stats.blockHeight==899272);
    delete shareQueue;
    puts("Candidate persistence: compatible stats layout, alternating CRC slots, failure/retry, reboot state, no replay and corrupt-slot fallback passed");
    puts("Block candidates: genesis proof/hash, full share queue, priority, deduplication, original height, immediate snapshot, delayed ACK/rejection, disconnect/ID reuse, stale work and explicit overflow passed");
}
