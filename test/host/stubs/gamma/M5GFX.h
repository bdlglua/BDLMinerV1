#pragma once
#include <map>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstring>
inline std::map<std::pair<int,int>,std::vector<uint8_t>> testI2c;
inline std::vector<std::vector<uint8_t>> testI2cWrites;
inline bool testI2cFail=false;
inline int testFailRegister=-1;
namespace lgfx { namespace i2c {
struct Result {bool value; bool has_value()const{return value;}};
inline Result transactionWriteRead(int,int address,const uint8_t *reg,uint8_t,uint8_t *out,size_t length,uint32_t){
    if(testI2cFail)return {false};
    auto it=testI2c.find({address,*reg});if(it==testI2c.end()||it->second.size()<length)return {false};
    memcpy(out,it->second.data(),length);return {true};
}
inline Result transactionWrite(int,int address,const uint8_t *bytes,uint8_t length,uint32_t){
    testI2cWrites.emplace_back(bytes,bytes+length);
    if(testI2cFail||bytes[0]==testFailRegister)return {false};
    if(length>1)testI2c[{address,bytes[0]}]=std::vector<uint8_t>(bytes+1,bytes+length);
    return {true};
}
}}
