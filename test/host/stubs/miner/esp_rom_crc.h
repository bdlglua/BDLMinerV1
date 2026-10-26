#pragma once
#include <cstdint>
#include <cstddef>
inline uint32_t esp_rom_crc32_le(uint32_t seed, const uint8_t *data, size_t length) {
    uint32_t crc = ~seed;
    while (length--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
