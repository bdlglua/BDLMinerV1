// Execute production ASIC work/verification/statistics adapters on the host.
#define BOARD_GAMMA601 1
#define BOARD_ASIC 1
#define CONFIG_IDF_TARGET_ESP32S3 1
#include "../../src/miner.cpp"
#include <cassert>
void logf(const char *, ...) {}
extern "C" bool sw_sha256d_prefilter(const uint32_t *,const uint8_t *,const uint32_t *){return false;}
namespace gamma601 {bool start(){return true;}}
namespace hwsha {
void idle(){} bool selfTest(){return false;}bool available(){return false;}bool fillAvailable(){return false;}
void prepare(Job &,const uint8_t *){}void prepareFill(Fill &,const uint8_t *){}uint32_t nonceAt(uint32_t n){return n;}
}
int main(){
    minerStart();assert(stats.minerCount==1&&!strcmp(stats.engine[0],"BM1370")&&testTaskStarts==0);
    work::Job job;job.id="asic-test";job.nbits=0x1d00ffff;job.version=0x20002000;job.versionMask=0x1fffe000;job.difficulty=1;
    minerPublishJob(job,true);AsicWork w;assert(minerAsicWork(w));assert(w.versionMask==job.versionMask);
    uint32_t base;memcpy(&base,w.header,4);assert(base==0x20000000);
    const char *genesis="0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c";
    assert(work::fromHex(genesis,w.header,80));w.versionMask=0;w.shareDifficulty=1;
    memset(w.networkTarget,0,32);
    stratum::Share share;
    assert(minerAsicCandidate(w,2083236893,0,3000)>2500);
    assert(!minerNextShare(share,0)&&stats.over32bits==0); // below the requested hardware filter
    minerAsicCandidate(w,2083236892,0);assert(!minerNextShare(share,0)&&stats.over32bits==0);
    minerAsicCandidate(w,2083236893,0x2000);assert(!minerNextShare(share,0)); // unnegotiated bits
    minerAsicCandidate(w,2083236893,0);assert(minerNextShare(share,0));
    assert(share.nonce==2083236893&&share.version==1&&share.versionMask==0&&share.difficulty>2536&&stats.over32bits==1);
    minerPublishJob(job,true);minerAsicCandidate(w,2083236893,0);assert(!minerNextShare(share,0)&&stats.over32bits==1);
    minerInvalidate();assert(!minerAsicWork(w));
    statsLoaded=false;stats.hashes[0]=0;lastHashes[0]=0;lastTickMs=testMillis;totalHashes=0;
    for(int i=0;i<60;++i){minerAsicHashes(1200000000000ULL);testMillis+=1000;minerTick();}
    assert(minerTotalHashes()==72000000000000ULL&&minerHashrate()==1.2e12&&minerEngineRate(0)==1.2e12);
    assert(stats.lifetimeHashes==72000000000000ULL);
    // Stop must decay to zero without unsigned overflow or imaginary work.
    for(int i=0;i<60;++i){testMillis+=1000;minerTick();}
    assert(minerHashrate()==0&&minerTotalHashes()==72000000000000ULL);
    // Answers may be out of order and arrive after a difficulty change.
    minerPoolConnected(true);
    minerShareSent(1.5,false,10);minerShareSent(2.5,true,11);minerShareSent(3.5,false,12);
    stats.poolDiff=100; stats.acceptedWork=0;
    minerShareResult(false,{11,0.5});minerShareResult(true,{12,0.25});
    ShareEntry log[20];int n=minerShareLog(log,20);
    assert(n==3&&log[0].difficulty==3.5f&&log[0].state==ShareAccepted&&log[1].state==ShareRejected&&log[1].backup&&log[2].state==SharePending);
    assert(stats.acceptedWork==0.25);
    minerPoolConnected(false);n=minerShareLog(log,20);assert(log[0].state==ShareAccepted&&log[2].state==ShareUnanswered);
    for(int i=0;i<25;++i)minerShareSent(i,false,100+i);
    assert(minerShareLog(log,20)==20&&log[0].difficulty==24.0f&&log[19].difficulty==5.0f);
    // A response for an entry evicted from the 20-row log still counts, but
    // must not settle a newer pending share.
    minerShareResult(true,{100,0.125});
    assert(stats.acceptedWork==0.375);
    minerShareLog(log,20);assert(log[19].state==SharePending);
    minerShareResult(true,{124,0.5});
    minerShareLog(log,20);assert(log[0].state==ShareAccepted&&stats.acceptedWork==0.875);
    double period=minerTakePeriodBest();assert(minerTakePeriodBest()==0&&period>=0);
    puts("Share log: out-of-order IDs, delayed vardiff credit, evicted entries, unanswered after disconnect and 20 newest kept passed");
    // A real SHA-256d ASIC result at the network target takes the candidate
    // path, retaining the independently recomputed genesis block identifier.
    minerPublishJob(job,true); assert(minerAsicWork(w));
    assert(work::fromHex(genesis,w.header,80)); w.versionMask=0;
    assert(work::targetFromBits(0x1d00ffff,w.networkTarget));
    uint32_t found=stats.blocksFound;
    minerAsicCandidate(w,2083236893,0);
    assert(stats.blocksFound==found+1 && minerNextShare(share,0));
    BlockCandidate candidate{}; assert(minerLastBlockCandidate(candidate));
    assert(candidate.number==found+1 && !memcmp(candidate.hash,share.hash,32));
    minerAsicCandidate(w,2083236893,0);
    assert(stats.blocksFound==found+1 && !minerNextShare(share,0));
    puts("ASIC block candidate: verified genesis proof, full hash, priority submission and duplicate suppression passed");
    activeMiningCoin=coin::DGB;job.version=0x20000100;minerPublishJob(job,true);assert(!minerAsicWork(w));
    job.version=0x20000000;job.versionMask=0x1fffe000;minerPublishJob(job,true);assert(minerAsicWork(w));
    job.versionMask=0x100;minerPublishJob(job,true);assert(!minerAsicWork(w));
    puts("Native ASIC mining: unique work/version masks, independent SHA-256d, corrupted/stale results, no software fallback and accurate 1.2 TH/s counters/stop decay passed");
}
