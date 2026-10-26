#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <unistd.h>
using String = std::string;
inline uint32_t millis() {
    static auto start=std::chrono::steady_clock::now();
    return 1+std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
}
inline void vTaskDelay(uint32_t ms) { usleep(std::min<uint32_t>(ms,1)*1000); }
#define pdMS_TO_TICKS(x) (x)
