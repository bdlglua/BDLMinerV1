#include "net_sv2.h"
#include "settings.h"
#include "miner.h"
#include <sodium/randombytes.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

WiFiClass WiFi; Settings settings;
String state;work::Job current;
int accepted=0,rejected=0,published=0,remaining=0,wdt=0;uint64_t counter=0;
bool connected=false, expectNoWork=false;
const String &netPoolHost(){return settings.poolHost;}
uint16_t netPoolPort(){return settings.poolPort;}
bool netOnBackup(){return false;}
void minerPoolConnected(bool v){connected=v;if(v&&expectNoWork)testClockStep=1000;}
void minerPoolDifficulty(double){}
void minerShareResult(bool ok,const stratum::ShareAck &){if(ok)++accepted;else ++rejected;}
void minerShareSent(double,bool,uint32_t,const uint8_t *){}
double minerHashrate(){return 1000000;}
void minerPublishJob(const work::Job &job,bool){current=job;++published;remaining=2;}
bool minerNextShare(stratum::Share &s,uint32_t){
    if(!remaining)return false;
    auto job=current;
#ifdef BOARD_ASIC
    assert(job.versionMask&0x2000);
    job.version^=0x2000; // verify real rolled-version proofs through the production session
#endif
    work::Work w;work::build(job,++counter,w);--remaining;
    s={};strcpy(s.jobId,w.jobId.c_str());strcpy(s.extranonce2,w.extranonce2Hex.c_str());
    s.ntime=w.ntime;s.version=job.version;
    for(s.nonce=0;s.nonce<4194304;++s.nonce) {
        if(sha::scan(w.mid,s.nonce,w.quickLimit,s.hash)&&work::meetsTarget(s.hash,w.shareTarget)) {
            s.difficulty=work::hashDifficulty(s.hash);return true;
        }
    }
    assert(false && "deterministic pool work did not produce an easy share");return false;
}
void core0WdtPause(){++wdt;}void core0WdtResume(){assert(--wdt>=0);}
void esp_fill_random(void *out,size_t size){randombytes_buf(out,size);}
void logf(const char *,...){}
int main(int argc,char **argv){
    if(argc!=3&&argc!=4)return 2;
    expectNoWork=argc==4&&!strcmp(argv[3],"no-work");
    settings.poolHost="test.pool";settings.poolPort=23330;settings.poolAuthority=argv[2];settings.btcAddress="address.worker";
    WiFiClient client(std::atoi(argv[1]));std::atomic<uint8_t> pending{0};
    auto result=sv2::session(client,0,"ManualMiner/test",[](const char *s){state=s;},pending);
    assert(wdt==0);
    if(state=="Pool needs version rolling") assert(!result.hasWork && result.refusal.empty());
    else assert(result.hasWork==(published>0));
    if(expectNoWork)assert(result.authorized&&!result.hasWork&&state=="SV2 work timed out"&&published==0);
    if(state=="SV2 protocol refused/invalid") assert(!result.refusal.empty());
    printf("%d %d %d %d %u %s\n",result.authorized,accepted,rejected,published,unsigned(pending.load()),state.c_str());
}
