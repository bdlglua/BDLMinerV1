#pragma once
#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include <cstring>
struct Preferences {
    static inline std::map<std::string, std::vector<uint8_t>> data;
    static inline bool failOpen = false, failWrite = false;
    bool begin(const char *, bool) { return !failOpen; }
    void end() {}
    size_t getBytesLength(const char *key) { return data[key].size(); }
    size_t getBytes(const char *key, void *out, size_t n) {
        if (data[key].size() != n) return 0;
        std::memcpy(out, data[key].data(), n); return n;
    }
    size_t putBytes(const char *key, const void *in, size_t n) {
        if (failWrite) return 0;
        const auto *p = static_cast<const uint8_t *>(in);
        data[key].assign(p, p + n); return n;
    }
    bool isKey(const char *) { return false; }
    double getDouble(const char *, double d) { return d; }
    uint64_t getULong64(const char *, uint64_t d) { return d; }
    uint32_t getULong(const char *, uint32_t d) { return d; }
};
