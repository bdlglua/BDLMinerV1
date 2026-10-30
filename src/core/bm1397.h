// BM1397 (Antminer S17 chip, one on the NerdNOS board) wire protocol.
// Derived from bitaxeorg/ESP-Miner and WantClue/NerdMiner_v2 (nerdnos branch),
// both GPL-3.0. Hardware references: docs/hardware/nerdnos.md.
//
// Unlike the BM1370, the chip takes SHA-256 midstates: one per rolled version
// (up to four), the last 4 merkle-root bytes, ntime and nbits.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace bm1397 {
constexpr uint32_t kVersionMask = 0x1fffe000;
constexpr size_t kSlots = 32;       // job ids 0, 4 ... 124: the low 2 bits name the midstate
constexpr uint8_t kMidstates = 4;
constexpr uint16_t kDefaultMHz = 300;  // NerdNOS firmware's setting, about 8 W
constexpr uint32_t kMaxBaud = 1562500;  // 25 MHz / ((1 + 1) * 8)

struct Packet { std::array<uint8_t, 160> bytes{}; size_t size = 0; };
// Header 0x2x: job with CRC16; otherwise command with CRC5.
Packet command(uint8_t header, const uint8_t *data, size_t length);
Packet reg(uint8_t address, uint32_t value);  // write, all chips
Packet readRegister(uint8_t address);         // read, all chips
Packet chainInactive();
Packet setAddress(uint8_t address);
Packet ticketMask(uint8_t power);             // results of difficulty >= 2^power
Packet maxBaud();                             // switches the chip to kMaxBaud
Packet pllDivider();
// PLL0 for `mhz` (bounded to 50-650), cgminer's gekko calculation.
Packet frequency(float mhz, float *actual = nullptr);

// Adds 1 to the bits of `value` selected by `mask`, carrying across them.
uint32_t incrementMask(uint32_t value, uint32_t mask);
// SHA-256 state after header bytes 0..63 with `version` in place, in the
// chip's order: state words 7..0, each little-endian.
void midstate(const uint8_t header[80], uint32_t version, uint8_t out[32]);
// A job for slot 0..31. `versions` receives the version of each midstate
// (four with a rolling mask, one without); `count` how many.
Packet job(uint8_t slot, const uint8_t header[80], uint32_t versionMask, uint32_t versions[kMidstates], uint8_t &count);

struct Response {
    bool chipId = false;
    uint8_t slot = 0, midstate = 0;
    uint32_t nonce = 0;  // as the header stores it
};
// 9-byte frames (AA 55 ...), resynchronized on the preamble after a lost byte.
// The CRC is not trusted to filter: every nonce is verified by hashing it.
class Receiver {
    std::array<uint8_t, 9> bytes{};
    size_t length = 0;
public:
    bool push(uint8_t byte, Response &out);
    void clear() { length = 0; }
};
}  // namespace bm1397
