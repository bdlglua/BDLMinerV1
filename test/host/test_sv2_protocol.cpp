#include "core/sv2_protocol.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using sv2::Frame; using sv2::Event;
static void u(std::vector<uint8_t> &v, uint32_t x, int size = 4) { while (size--) { v.push_back(x); x >>= 8; } }
static Frame success() { return Frame(0, 1, {2,0,0,0,0,0}); }
static Frame channel() {
    Frame f(0, 0x14); auto &v = f.payload; u(v,1); u(v,7); v.insert(v.end(),32,255);
    u(v,8,2); v.push_back(2); v.push_back(0xaa); v.push_back(0xbb); u(v,11); return f;
}
static Frame job(uint32_t id, bool future, uint32_t time = 1800000000) {
    Frame f(0x8000, 0x1f); auto &v=f.payload; u(v,11); u(v,id); v.push_back(!future);
    if (!future) u(v,time);
    u(v,0x20000000); v.push_back(0); v.push_back(1); v.insert(v.end(),32,0x11);
    u(v,2,2); v.push_back(1); v.push_back(2); u(v,1,2); v.push_back(3); return f;
}
static Frame previous(uint32_t id) {
    Frame f(0x8000,0x20); auto &v=f.payload; u(v,11); u(v,id); v.insert(v.end(),32,0x22);
    u(v,1800000000); u(v,0x1d00ffff); return f;
}
static Frame ack(uint32_t last, uint32_t count) {
    Frame f(0x8000,0x1c); auto &v=f.payload; u(v,7);u(v,last);u(v,count);u(v,0);u(v,0);return f;
}
static stratum::Share share(const work::Work &w, uint32_t nonce=123) {
    stratum::Share s{}; strcpy(s.jobId,w.jobId.c_str());strcpy(s.extranonce2,w.extranonce2Hex.c_str());
    s.ntime=w.ntime;s.nonce=nonce;s.version=0x20000000;
    assert(sha::scan(w.mid,nonce,UINT32_MAX,s.hash)); s.difficulty=work::hashDifficulty(s.hash); return s;
}
int main(int argc, char **) {
    sv2::Protocol p;
    auto setup = p.setup("p",3333,"a","v");
    const uint8_t expected[] = {0,2,0,2,0,0,0,0,0,1,'p',5,13,1,'a',5,'E','S','P','3','2',1,'v',0};
    assert(setup.type==0 && setup.payload==std::vector<uint8_t>(expected,expected+sizeof(expected)));
    assert(p.handle(success())==Event::Setup);
    auto open=p.open("address.worker",1000000);
    assert(open.type==0x13 && open.payload[4]==14 && open.payload[open.payload.size()-2]==8);
    assert(p.handle(channel())==Event::Opened && p.opened());
    assert(p.handle(job(42,true))==Event::None && !p.hasJob());
    assert(p.handle(previous(42))==Event::Job && p.job().clean);
    work::Work w;work::build(p.job(),10,w);
    assert(w.extranonce2Hex=="000000000000000a" && w.exactShareTarget && w.quickLimit==UINT32_MAX);
    // Python independently verifies this header's coinbase, merkle root and hash.
    if(argc>1){ auto s=share(w);auto submitted=p.submit(s);
        printf("%s\n%s\n",work::toHex(w.header,80).c_str(),work::toHex(submitted.payload.data(),submitted.payload.size()).c_str());return 0;}
    auto s=share(w);assert(!p.submit(s).payload.empty());assert(!p.submit(s).payload.empty());assert(!p.submit(s).payload.empty());
    assert(p.pending()==3);
    Frame rejected(0x8000,0x1d);u(rejected.payload,7);u(rejected.payload,1);rejected.payload.push_back(3);
    rejected.payload.insert(rejected.payload.end(),{'b','a','d'});
    assert(p.handle(rejected)==Event::Rejected && p.acknowledged()==1 && p.pending()==2);
    assert(p.handle(rejected)==Event::None && p.acknowledged()==0);
    auto invalid=p;
    assert(invalid.handle(ack(2,3))==Event::Error && invalid.pending()==2);
    assert(p.handle(ack(2,2))==Event::Accepted && p.acknowledged()==2 && p.pending()==0);
    assert(p.handle(ack(2,2))==Event::None && p.acknowledged()==0);
    assert(p.handle(job(43,false,1800000001))==Event::Job && !p.job().clean);
    auto bounded=p;
    for(int i=0;i<32;++i)assert(!bounded.submit(s).payload.empty());
    assert(bounded.pending()==32 && bounded.submit(s).payload.empty());
    assert(bounded.handle(ack(34,32))==Event::Accepted && bounded.acknowledged()==32 && bounded.pending()==0);
    assert(!p.submit(s).payload.empty()); // previous job remains valid on the same previous block
    Frame target(0x8000,0x21);u(target.payload,7);target.payload.resize(36,0);target.payload[4]=1;
    assert(p.handle(target)==Event::Target && p.job().exactShareTarget);
    assert(p.submit(s).payload.empty()); // recheck exact U256 after a target change

    // Jobs received with an explicit ntime keep their original target. Future
    // jobs, including one activated by SetNewPrevHash, follow SetTarget.
    sv2::Protocol targets; assert(targets.handle(success())==Event::Setup); assert(targets.handle(channel())==Event::Opened);
    assert(targets.handle(job(1,true))==Event::None); assert(targets.handle(previous(1))==Event::Job);
    work::Work future; work::build(targets.job(),1,future); auto futureShare=share(future);
    assert(targets.handle(job(2,false))==Event::Job);
    work::Work fixed; work::build(targets.job(),2,fixed); auto fixedShare=share(fixed);
    assert(targets.handle(target)==Event::Target);
    assert(targets.job().difficulty==fixed.shareDifficulty);
    work::Work unchanged; work::build(targets.job(),2,unchanged);
    assert(!memcmp(unchanged.shareTarget,fixed.shareTarget,32));
    assert(!targets.submit(fixedShare).payload.empty());
    assert(targets.submit(futureShare).payload.empty());
    assert(targets.handle(job(3,false))==Event::Job); work::Work harder; work::build(targets.job(),3,harder);
    assert(!memcmp(harder.shareTarget,target.payload.data()+4,32));
    // A pending future job adopts the newest target before activation, and
    // keeps following target updates when it becomes the active work.
    assert(targets.handle(job(4,true))==Event::None);
    Frame easier=target; for(size_t i=4;i<36;++i)easier.payload[i]=255;
    assert(targets.handle(easier)==Event::Target);
    assert(targets.handle(previous(4))==Event::Job && targets.job().shareTarget[31]==255);
    assert(targets.submit(fixedShare).payload.empty()); // previous-block work invalidated
    assert(targets.handle(target)==Event::Target && targets.job().shareTarget[31]==0);
    work::Work active;work::build(targets.job(),4,active);
    assert(!memcmp(active.shareTarget,target.payload.data()+4,32));
    for (const auto &frame : {success(),channel(),job(42,true),previous(42),ack(2,2),target,rejected}) {
        for(size_t n=0;n<frame.payload.size();++n){
            sv2::Protocol parser;
            assert(parser.handle(success())==Event::Setup);assert(parser.handle(channel())==Event::Opened);
            Frame cut=frame;cut.payload.resize(n);assert(parser.handle(cut)==Event::Error);
        }
    }
    sv2::Protocol cache;assert(cache.handle(success())==Event::Setup);assert(cache.handle(channel())==Event::Opened);
    assert(cache.handle(previous(999))==Event::Error);
    auto malformed=job(44,true);malformed.payload[8]=2;assert(cache.handle(malformed)==Event::Error);
    Frame foreign=job(44,true);foreign.payload[0]=99;assert(cache.handle(foreign)==Event::Error);
    assert(cache.handle(job(44,true))==Event::None);assert(cache.handle(job(44,true))==Event::Error);
    puts("SV2 protocol: exact wire layout, extended jobs, unique extranonces, clean/non-clean jobs, exact targets, batched acknowledgments, duplicate rejection and all truncated frames passed");
}
