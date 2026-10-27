#pragma once
#include "../miner/Arduino.h"
#include <deque>
#include <vector>
constexpr int OUTPUT=1, LOW=0, HIGH=1, SERIAL_8N1=0;
inline int testReset=0;
inline void pinMode(int,int) {}
inline void digitalWrite(int pin,int value) { if(pin==1)testReset=value; }
inline void delay(uint32_t ms) {testMillis+=ms;}
inline void vTaskDelete(void *) {}
inline std::deque<uint8_t> testUartRx;
inline std::vector<std::vector<uint8_t>> testUartTx;
inline bool testUartFail=false;
inline std::vector<uint8_t> testChipReply;
class HardwareSerial {
public:
    HardwareSerial(int) {}
    void end() {} void begin(int,int,int,int) {} void updateBaudRate(int) {}
    void setRxBufferSize(int) {} void flush() {}
    int available(){return testUartRx.size();}
    int read(){if(testUartRx.empty())return -1;int b=testUartRx.front();testUartRx.pop_front();return b;}
    size_t write(const uint8_t *bytes,size_t length){
        if(testUartFail)return 0;
        testUartTx.emplace_back(bytes,bytes+length);
        if(length==7&&bytes[2]==0x52&&bytes[5]==0)
            for(auto b:testChipReply)testUartRx.push_back(b);
        return length;
    }
};
