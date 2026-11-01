#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <unistd.h>
using String = std::string;
// Only the no-work session regression advances time, after the channel opens.
inline uint32_t testClockOffset = 0, testClockStep = 0;
inline uint32_t millis() {
    static auto start=std::chrono::steady_clock::now();
    return 1+testClockOffset+std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
}
inline void vTaskDelay(uint32_t ms) { testClockOffset += testClockStep; usleep(std::min<uint32_t>(ms,1)*1000); }
#define pdMS_TO_TICKS(x) (x)
