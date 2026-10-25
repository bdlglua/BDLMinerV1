#pragma once

// Minimal declarations for compiling miner.cpp on the host. These stubs do
// not schedule tasks; the queue test explicitly orders delayed callbacks.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

using String = std::string;
using std::min;
using std::max;
using UBaseType_t = unsigned;
using TickType_t = uint32_t;
using portMUX_TYPE = int;

constexpr int portNUM_PROCESSORS = 2;
constexpr int portMUX_INITIALIZER_UNLOCKED = 0;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
constexpr int pdTRUE = 1;

#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)

inline uint32_t testMillis = 1000;
inline uint32_t millis() { return testMillis; }
inline void vTaskDelay(uint32_t) {}
inline void xTaskCreatePinnedToCore(void (*)(void *), const char *, int, void *, unsigned, void *, int) {}

// glibc >= 2.38 fournit déjà strlcpy dans <string.h> : on ne définit
// notre version que si la libc du système ne l'a pas.
#if !(defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 38)))
inline size_t strlcpy(char *dst, const char *src, size_t n) {
    if (n) {
        std::strncpy(dst, src, n - 1);
        dst[n - 1] = 0;
    }
    return std::strlen(src);
}
#endif
