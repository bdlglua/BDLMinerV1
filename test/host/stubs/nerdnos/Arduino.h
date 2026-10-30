#pragma once
// Host stubs for the NerdNOS backend: pins, ADC and a BM1397 that answers
// the chip-id read with `testChips` frames.
#include "../miner/Arduino.h"
#include <deque>
#include <map>
#include <vector>
constexpr int OUTPUT = 1, LOW = 0, HIGH = 1, SERIAL_8N1 = 0, ADC_11db = 3;
inline std::map<int, int> testPins;
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int value) { testPins[pin] = value; }
inline uint32_t testMilliVolts = 1650;
inline void analogSetPinAttenuation(int, int) {}
inline uint32_t analogReadMilliVolts(int) { return testMilliVolts; }
inline void delay(uint32_t ms) { testMillis += ms; }
inline std::deque<uint8_t> testUartRx;
inline std::vector<std::vector<uint8_t>> testUartTx;
inline int testChips = 1;
inline uint32_t testBaud = 0;
class HardwareSerial {
public:
    HardwareSerial(int) {}
    void end() {}
    void begin(int baud, int, int, int) { testBaud = baud; }
    void updateBaudRate(int baud) { testBaud = baud; }
    void setRxBufferSize(int) {}
    void flush() {}
    int available() { return testUartRx.size(); }
    int read() { if (testUartRx.empty()) return -1; int b = testUartRx.front(); testUartRx.pop_front(); return b; }
    size_t write(const uint8_t *bytes, size_t length) {
        testUartTx.emplace_back(bytes, bytes + length);
        if (length == 7 && bytes[2] == 0x52 && bytes[5] == 0)
            for (int i = 0; i < testChips; ++i)
                for (uint8_t b : {0xaa, 0x55, 0x13, 0x97, 0x18, 0x00, 0x00, 0x00, 0x06}) testUartRx.push_back(b);
        return length;
    }
};
