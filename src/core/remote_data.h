// Opt-in telemetry identifiers; no raw MAC, payout address or pool host leaves.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "sha256.h"

namespace remote_data {
inline bool validKey(const char *key) {
    if (!key || std::strlen(key) != 64) return false;
    for (unsigned i = 0; i < 64; ++i)
        if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f'))) return false;
    return true;
}
inline void deviceId(const char *key, uint64_t mac, char out[17]) {
    uint8_t input[72], digest[32];
    std::memcpy(input, key, 64);
    for (unsigned i = 0; i < 8; ++i) input[64+i] = uint8_t(mac >> (i*8));
    sha::sha256(input, sizeof(input), digest);
    static constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i) { out[i*2] = hex[digest[i] >> 4]; out[i*2+1] = hex[digest[i] & 15]; }
    out[16] = 0;
    std::memset(input, 0, sizeof(input));
}
} // namespace remote_data
