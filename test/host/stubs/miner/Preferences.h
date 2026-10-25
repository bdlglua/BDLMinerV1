#pragma once
#include <cstdint>
#include <cstddef>
struct Preferences {
    bool begin(const char *, bool) { return true; }
    void end() {}
    size_t getBytesLength(const char *) { return 0; }
    size_t getBytes(const char *, void *, size_t) { return 0; }
    size_t putBytes(const char *, const void *, size_t n) { return n; }
    bool isKey(const char *) { return false; }
    double getDouble(const char *, double d) { return d; }
    uint64_t getULong64(const char *, uint64_t d) { return d; }
    uint32_t getULong(const char *, uint32_t d) { return d; }
};
