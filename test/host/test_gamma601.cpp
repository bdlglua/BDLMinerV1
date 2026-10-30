#define BOARD_GAMMA601 1
#define BOARD_ASIC 1
#include "../../src/gamma601.cpp"
#include <cassert>
void logf(const char *, ...) {}
Settings settings;
static uint8_t power=100;
static uint32_t generation=1, powerUntil=UINT32_MAX;
static uint64_t hashes=0;
uint8_t minerPowerPercent(){return testMillis < powerUntil ? power : 0;}
bool minerAsicCurrent(uint32_t g){return g==generation;}
bool minerAsicWork(AsicWork &){return false;}
void minerAsicCandidate(AsicWork &,uint32_t,uint32_t){}
void minerAsicHashes(uint64_t n){hashes+=n;}
using namespace gamma601;
static void bytes(int addr,int reg,std::initializer_list<uint8_t> value){testI2c[{addr,reg}]=value;}
static void word(int reg,uint16_t v){bytes(0x24,reg,{uint8_t(v),uint8_t(v>>8)});}
static uint16_t storedWord(int reg) {
    const auto &v=testI2c.at({0x24,reg});assert(v.size()==2);
    return uint16_t(v[0])|uint16_t(v[1])<<8;
}
static void health(){
    bytes(0x4c,1,{50});bytes(0x4c,0x10,{0});bytes(0x4c,0x46,{0x08});bytes(0x4c,0x47,{0x07});
    word(0x88,bm1370::encodeLinear11(5));word(0x8b,589);word(0x8c,bm1370::encodeLinear11(15));
    word(0x8d,bm1370::encodeLinear11(50));word(0x79,0);
}
static void reset(){
    latched=powered=configured=forceStock=false;settings=Settings();settings.asicFrequency=525;
    live={NAN,NAN,NAN,NAN,NAN,0,0,0,false,"Starting",525,false,"",-1};publish();
    testI2c.clear();testI2cWrites.clear();testI2cFail=testUartFail=false;testFailRegister=-1;
    testUartTx.clear();testUartTxTimes.clear();testUartRx.clear();testDelayHook=nullptr;testMillis=1000;testReset=1;power=100;generation=1;powerUntil=UINT32_MAX;
    bytes(0x4c,0xfd,{0x16});bytes(0x24,0xad,{6,0x54,0x49,0x54,0x6d,0x24,0x41});bytes(0x24,0x20,{0x17});health();
    testChipReply={0xaa,0x55,0x13,0x70,0,0,0,0,0,0,0};
    for(int crc=0;crc<32;++crc){testChipReply[10]=crc;if(bm1370::crc5(testChipReply.data()+2,9)==0)break;}
}
int main(){
    reset();begin();assert(configured&&!latched&&testReset==0);
    assert(testI2c[std::make_pair(0x24,1)][0]==0);
    assert(testI2c[std::make_pair(0x4c,0x4c)][0]==63);
    // TPS546 VOUT limit/margin registers store fractions of VOUT_COMMAND.
    // Independent nearest ULINEAR16 words for exponent -9, NOT volts * 1.15.
    assert(storedWord(0x40)==640); // 1.25x
    assert(storedWord(0x42)==594); // 1.16x
    assert(storedWord(0x25)==563); // 1.10x
    assert(storedWord(0x26)==461 && storedWord(0x43)==461); // 0.90x
    assert(storedWord(0x44)==384); // 0.75x
    assert(storedWord(0x21)==589); // only VOUT_COMMAND is an absolute voltage
    for(auto &w:testI2cWrites)assert(w[0]!=0x11&&w[0]!=0x15&&w[0]!=0x16); // no STORE/RESTORE NVM commands
    AsicWork w;w.generation=1;w.versionMask=bm1370::kVersionMask;
    assert(initAsic(w)&&live.running&&powered&&testReset==1&&live.frequency==525);
    assert(testI2c[std::make_pair(0x24,1)][0]==0x80);
    uint32_t lastPllAt=0;int pllCount=0;
    for(size_t i=0;i<testUartTx.size();++i) {
        const auto &packet=testUartTx[i];
        if(packet.size()!=11||packet[5]!=0x08)continue;
        if(pllCount)assert(testUartTxTimes[i]-lastPllAt>=100);
        lastPllAt=testUartTxTimes[i];++pllCount;
    }
    assert(pllCount==77); // 50 -> 525 MHz in 6.25 MHz steps
    assert(request(0x8c,false));const auto &poll=testUartTx.back();
    assert(poll.size()==7&&poll[2]==0x42&&poll[4]==0&&poll[5]==0x8c);

    bytes(0x4c,1,{70});assert(!collect(true)&&latched&&!powered&&testReset==0);
    assert(testI2c[std::make_pair(0x24,1)][0]==0&&testI2c[std::make_pair(0x4c,0x4c)][0]==63);
    reset();begin();assert(initAsic(w));off("Thermal pause");assert(!latched&&testReset==0);
    bytes(0x4c,1,{40});assert(collect(false)&&temperature()==40); // cooldown still observable while unpowered
    reset();bytes(0x24,0xad,{6,0,0,0,0,0,0});begin();assert(!configured&&latched&&testReset==0);
    reset();testFailRegister=0x21;begin();assert(!configured&&latched&&testReset==0);
    reset();begin();assert(initAsic(w));testI2cFail=true;assert(!collect(true)&&latched&&testReset==0&&!powered);
    reset();begin();bytes(0x4c,0x46,{0xff});bytes(0x4c,0x47,{0xff});assert(!initAsic(w)&&latched&&testReset==0);
    reset();begin();power=0;assert(!initAsic(w)&&!powered&&testReset==0&&!latched);
    reset();begin();generation=2;assert(!initAsic(w)&&!powered&&testReset==0);
    reset();begin();powerUntil=1250;assert(!initAsic(w)&&!powered&&testReset==0&&!latched);
    reset();begin();
    testDelayHook=[](uint32_t){
        int pll=0;for(auto &p:testUartTx)if(p.size()==11&&p[5]==8)++pll;
        if(pll>=20)bytes(0x4c,1,{70});
    };
    assert(!initAsic(w)&&latched&&!powered&&testReset==0&&temperature()==70);
    reset();begin();
    testDelayHook=[](uint32_t){
        int pll=0;for(auto &p:testUartTx)if(p.size()==11&&p[5]==8)++pll;
        if(pll>=20){bytes(0x4c,0x46,{0xff});bytes(0x4c,0x47,{0xff});}
    };
    assert(!initAsic(w)&&latched&&!powered&&testReset==0);
    reset();begin();
    testDelayHook=[](uint32_t){
        int pll=0;for(auto &p:testUartTx)if(p.size()==11&&p[5]==8)++pll;
        if(pll>=20)power=50;
    };
    assert(initAsic(w)&&live.running&&live.frequency==262.5f);
    reset();begin();powerUntil=5000;assert(!initAsic(w)&&!powered&&testReset==0&&!latched);
    // Allow the regulator to reach power-good before monitoring startup voltage.
    reset();begin();word(0x8b,0);word(0x79,0x0840);
    testDelayHook=[](uint32_t){
        if(powered&&millis()-powerEnabledAt>=200){word(0x8b,589);word(0x79,0);}
    };
    assert(initAsic(w)&&live.running);
    // The diode is allowed to be unavailable until its ASIC mux and ADC settle.
    reset();begin();bytes(0x4c,1,{0x7f});bytes(0x4c,0x10,{0});
    testDelayHook=[](uint32_t){
        for(size_t i=0;i<testUartTx.size();++i){const auto &p=testUartTx[i];
            if(p.size()==11&&p[5]==0x54&&p[9]==2&&millis()-testUartTxTimes[i]>=65)
                bytes(0x4c,1,{50});
        }
    };
    assert(initAsic(w)&&live.running&&temperature()==50);
    reset();begin();testUartFail=true;assert(!initAsic(w));off("UART failed",true);assert(latched&&testReset==0);
    reset();begin();testTaskFailAt=testTaskStarts;assert(!start()&&latched&&testReset==0);
    assert(Settings().asicFrequency==600);
    for(unsigned profile=525;profile<=800;profile+=25){
        reset();settings.asicFrequency=profile;begin();
        assert(initAsic(w)&&live.running&&live.frequency==profile&&live.requestedMHz==profile);
        assert(storedWord(0x21)==589); // 1150 mV, including at 800 MHz
        assert(testI2c[std::make_pair(0x4c,0x4c)][0]==63); // full fan
    }
    reset();settings.asicFrequency=800;begin();assert(initAsic(w)&&live.frequency==800);
    // Error/total register values are independent cumulative counters, paired by time.
    bm1370::Response totalReply{};totalReply.reg=0x8c;
    bm1370::Response errorReply{};errorReply.reg=0x4c;
    for(int i=0;i<=30;++i){testMillis+=1000;errorReply.value=i*10;totalReply.value=i*300;
        registerResult(errorReply);registerResult(totalReply);
    }
    assert(forceStock&&live.stockFallback&&!powered&&!latched&&testReset==0);
    assert(!strcmp(live.fallbackReason,"High ASIC hardware error rate"));
    assert(targetFrequency()==525&&initAsic(w)&&live.frequency==525);
    off("Pool reconnect");assert(forceStock&&targetFrequency()==525); // no automatic OC retry
    assert(initAsic(w));instability("ASIC counter stalled; restart required");assert(latched&&!powered);
    reset();settings.asicFrequency=600;begin();assert(initAsic(w));
    instability("ASIC not responding; restart required");assert(forceStock&&!latched&&testReset==0);
    reset();settings.asicFrequency=600;begin();assert(initAsic(w));
    testMillis+=3100;checkWatchdogs(testMillis);assert(forceStock&&!latched&&!powered);
    reset();settings.asicFrequency=600;begin();assert(initAsic(w));
    testMillis+=5100;lastHashReply=lastHashProgress=testMillis;
    checkWatchdogs(testMillis);assert(forceStock&&!latched&&!powered);
    assert(!strcmp(live.fallbackReason,"ASIC error telemetry unavailable"));
    reset();settings.asicFrequency=600;begin();assert(initAsic(w));
    testMillis+=5100;lastHashReply=testMillis;lastErrorReply=testMillis;
    checkWatchdogs(testMillis);assert(forceStock&&!latched&&!powered);
    assert(!strcmp(live.fallbackReason,"ASIC counter stalled; restart required"));
    reset();settings.asicFrequency=600;begin();assert(initAsic(w));
    testI2cFail=true;instability("High ASIC hardware error rate");
    assert(latched&&!powered&&testReset==0); // failed shutdown cannot auto-restart
    reset();settings.asicFrequency=600;begin();assert(initAsic(w));
    bytes(0x4c,1,{70});assert(!collect(true)&&latched&&!forceStock&&!powered); // hard limits still stop
    reset();settings.asicFrequency=800;begin();assert(initAsic(w));
    testMillis+=3100;checkWatchdogs(testMillis);assert(forceStock&&!latched&&!powered);
    assert(initAsic(w)&&live.frequency==525); // highest profile still falls back to stock
    reset();settings.asicFrequency=800;begin();assert(initAsic(w));
    bytes(0x4c,1,{70});assert(!collect(true)&&latched&&!forceStock&&!powered);
    puts("Gamma hardware backend: volatile regulator configuration, native BM1370 startup, relative VOUT limits, supervised PLL ramp, fan/temperature/I2C faults, reset-before-shutdown, pause/cooldown, OTA/supervision lease and stale jobs passed");
}
