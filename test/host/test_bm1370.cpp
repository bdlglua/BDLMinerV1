// Independent wire vectors and electrical interlocks for the Gamma backend.
#include "core/bm1370.h"
#include "core/stratum.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace bm1370;
static void golden(const Packet &p, const char *hex) { assert(work::toHex(p.bytes.data(),p.size)==hex); }
static std::vector<uint8_t> response(const char *hex) {
    std::vector<uint8_t> out; assert(work::fromHex(hex,out)); return out;
}
int main() {
    golden(versionMask(0x1fffe000),"55aa510900a49000ffff1c");
    golden(versionMask(0),"55aa510900a49000000017");
    golden(reg(0xa8,0x00070000),"55aa510900a80007000003");
    const uint32_t masks[]{0,0xe0,0xf0,0xff,0xf8ff};
    const uint8_t powers[]{0,3,4,8,13};
    for(int i=0;i<5;++i) {
        auto p=ticketMask(powers[i]);
        uint32_t value=uint32_t(p.bytes[6])<<24|uint32_t(p.bytes[7])<<16|uint32_t(p.bytes[8])<<8|p.bytes[9];
        assert(value==masks[i]);
    }
    assert(!ticketMask(32).size);
    float actual=0;auto pll=frequency(525,&actual);
    assert(pll.size==11&&std::fabs(actual-525)<.01);
    assert(!frequency(NAN).size&&!frequency(526).size&&!frequency(0).size);
    for(int f=50;f<=525;++f){assert(frequency(f,&actual).size&&std::fabs(actual-f)<3);}
    uint8_t header[80];for(int i=0;i<80;++i)header[i]=i;
    auto p=job(15,header);assert(p.size==88&&p.bytes[4]==120&&p.bytes[5]==1);
    assert(!memcmp(p.bytes.data()+6,header+76,4));
    assert(!memcmp(p.bytes.data()+10,header+72,4));
    assert(!memcmp(p.bytes.data()+14,header+68,4));
    for(int w=0;w<8;++w){
        assert(!memcmp(p.bytes.data()+18+4*w,header+36+4*(7-w),4));
        assert(!memcmp(p.bytes.data()+50+4*w,header+4+4*(7-w),4));
    }
    assert(!memcmp(p.bytes.data()+82,header,4));
    // Python binascii.crc_hqx(bytes(range fixture wire), 0xffff) -> 0xd6e1.
    assert(p.bytes[86]==0xd6&&p.bytes[87]==0xe1);
    assert(!job(16,header).size);
    Receiver rx;Response decoded;
    auto wire=response("aa559b044c0a0030112288"); // independent CRC patched below
    wire[10]=0x80;
    for(int i=0;i<32;++i){wire[10]=0x80|i;if(crc5(wire.data()+2,9)==0)break;}
    int hits=0;
    auto feed=[&](const std::vector<uint8_t>&data){for(auto b:data)if(rx.push(b,decoded))++hits;};
    feed({0,0xaa,1,0xaa});feed(wire);
    assert(hits==1&&decoded.job&&decoded.slot==3&&decoded.nonce==0x0a4c049b&&decoded.versionBits==0x02244000);
    for(int bit=0;bit<72;++bit){auto bad=wire;bad[2+bit/8]^=1u<<(bit%8);feed(bad);assert(hits==1);}
    feed(wire);assert(hits==2);
    auto chip=response("aa55137000000000000000");
    for(int i=0;i<32;++i){chip[10]=i;if(crc5(chip.data()+2,9)==0)break;}
    feed(chip);assert(hits==3&&!decoded.job&&(decoded.value>>16)==0x1370);
    assert(std::fabs(linear11(encodeLinear11(4.8))-4.8)<.01);
    assert(std::fabs(linear11(encodeLinear11(-12.5))+12.5)<.01);
    uint16_t v;assert(voltageWord(1.15,0x17,v)&&v==589);
    assert(!voltageWord(1.15,0x37,v)&&!voltageWord(NAN,0x17,v)&&!voltageWord(3,0x17,v));
    assert(diodeTemperature(55,0x80)==55.5f);
    assert(std::isnan(diodeTemperature(0x7f,0x00))&&std::isnan(diodeTemperature(0x7f,0xe0)));
    assert(fanRpm(1800)==3000&&fanRpm(0)==0&&fanRpm(0xffff)==0&&fanRpm(1)==0);
    Health h{55,60,5.0,1.15,15,3000,0};assert(!h.fault(true));
    auto healthy=h;
    h.temperature=70;assert(h.fault(true));h=healthy;h.temperature=NAN;assert(h.fault(true));
    assert(!h.fault(false,false));h=healthy;h.rpm=0;assert(h.fault(false,false));
    h=healthy;h.inputVoltage=4.7;assert(h.fault(true));h=healthy;h.regulatorTemperature=90;assert(h.fault(true));
    h=healthy;h.coreVoltage=1.3;assert(h.fault(true));h=healthy;h.current=30;assert(h.fault(true));
    for(uint16_t flag:{0x8000,0x4000,0x2000,0x1000,0x0200,0x0020,0x0010,0x0008,0x0004,0x0002}){
        h=healthy;h.status=flag;assert(h.fault(true)&&h.fault(false,false));
    }
    h=healthy;h.status=0x0840;h.coreVoltage=0;assert(!h.fault(false));assert(h.fault(true));
    Counter c;assert(!c.update(UINT32_MAX-100,100));
    assert(c.update(99,1100)==(uint64_t(200)<<32));
    assert(!c.update(0,2100));assert(c.update(250,3100)==(uint64_t(250)<<32));
    c.reset();assert(!c.update(500,4100));assert(!c.update(1000,10100));
    assert(c.update(1233,11100)==1000727379968ULL);
    stratum::Protocol proto;proto.reset();auto configure=proto.configureVersionRolling();
    JsonDocument json;assert(!deserializeJson(json,configure));assert(json["method"]=="mining.configure");
    proto.handleLine(R"({"id":4,"result":{"version-rolling":true,"version-rolling.mask":"1fffe000"},"error":null})");
    stratum::Share share{};strcpy(share.jobId,"a");strcpy(share.extranonce2,"00000001");
    share.versionMask=kVersionMask;share.version=0x20002000;
    assert(!deserializeJson(json,proto.submit(share))&&json["params"].size()==6&&json["params"][5]=="00002000");
    proto.reset();assert(proto.submit(share).empty());share.versionMask=0;
    assert(!deserializeJson(json,proto.submit(share))&&json["params"].size()==5);
    proto.configureVersionRolling();
    assert(proto.handleLine(R"({"id":4,"result":{"version-rolling":true,"version-rolling.mask":"ffffffff"}})")==stratum::Event::Error);
    proto.reset();
    assert(proto.handleLine(R"({"method":"mining.set_version_mask","params":["1fffe000"]})")==stratum::Event::Error);
    puts("BM1370: wire vectors, fragmented/corrupt UART, PLL, PMBus, thermal/fan/supply interlocks, 64-bit counter and BIP310 passed");
}
