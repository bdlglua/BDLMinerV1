// Independent Python oracle and repeatable native benchmark for SHA-256d.
#include "core/sha256.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void hex(const uint8_t *bytes) {
    for (unsigned i=0;i<32;++i) std::printf("%02x",bytes[i]);
}

int main(int argc,char **argv) {
    if(argc>1 && !std::strcmp(argv[1],"bench")) {
        unsigned count=argc>2?std::strtoul(argv[2],nullptr,10):400000;
        if(!count)return 2;
        for(unsigned length:{64u,80u}) {
            uint8_t data[80]{},digest[32];unsigned guard=0;
            auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<count;++i) {
                std::memcpy(data,&i,sizeof(i));
                sha::sha256d(data,length,digest);guard^=digest[i&31];
            }
            double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            std::printf("%u %u %.9f %u\n",length,count,seconds,guard);
        }
        return 0;
    }
    for(unsigned length=0;length<=4096;++length) {
        if(length>512 && length!=1023 && length!=1024 && length!=4095 && length!=4096)continue;
        std::vector<uint8_t> data(length+65);uint32_t seed=0x93ac5f17u^length;
        for(unsigned i=0;i<length;++i)data[i+1]=(seed=seed*1664525u+1013904223u)>>24;
        uint8_t one[32],two[32];const uint8_t *input=length?data.data()+1:nullptr;
        sha::sha256(input,length,one);sha::sha256d(input,length,two);
        sha::sha256d(data.data()+1,length,data.data()+1);
        std::printf("%u ",length);hex(one);std::printf(" ");hex(two);std::printf(" ");hex(data.data()+1);std::puts("");
    }
}
