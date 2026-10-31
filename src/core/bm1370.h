// Bitaxe Gamma 601 wire protocol. Derived from bitaxeorg/ESP-Miner (GPL-3.0).
// Upstream revision and hardware references: docs/hardware/bitaxe-gamma-601.md.
#pragma once
#include "work.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace bm1370 {
constexpr uint32_t kVersionMask = 0x1fffe000;
constexpr size_t kSlots = 16;
constexpr uint16_t kStockMHz = 525, kMaxMHz = 800;
inline bool validProfile(unsigned mhz) { return mhz >= kStockMHz && mhz <= kMaxMHz && (mhz-kStockMHz)%25 == 0; }
// Compare cumulative ASIC error/total counters in ten-second windows.
struct ErrorMonitor {
    uint32_t errors = 0, total = 0, at = 0;
    uint8_t badWindows = 0;
    bool ready = false;
    float percent = -1;
    void reset() { ready = false; badWindows = 0; percent = -1; }
    bool update(uint32_t errorValue, uint32_t totalValue, uint32_t now);
};
struct Packet { std::array<uint8_t, 128> bytes{}; size_t size = 0; };
uint8_t crc5(const uint8_t *data, size_t length);
Packet command(uint8_t header, const uint8_t *data, size_t length);
Packet reg(uint8_t address, uint32_t value, bool broadcast = true);
Packet job(uint8_t slot, const uint8_t header[80]);
Packet versionMask(uint32_t mask);
Packet ticketMask(uint8_t power);
Packet frequency(float mhz, float *actual = nullptr);

struct Response {
    bool job = false;
    uint8_t slot = 0, address = 0, reg = 0;
    uint32_t nonce = 0, versionBits = 0, value = 0;
};
// Incremental resynchronization and CRC validation, including fragmented UART reads.
class Receiver {
    std::array<uint8_t, 11> bytes{};
    size_t length = 0;
public:
    bool push(uint8_t byte, Response &out);
    void clear() { length = 0; }
};
float linear11(uint16_t word);
uint16_t encodeLinear11(float value);
// A whole number with the smallest exponent from 0 up, as ESP-Miner writes
// temperatures and timings (0 -> 0x0000, 105 -> 0x0069).
uint16_t encodeLinear11Int(unsigned value);
bool voltageWord(float volts, uint8_t mode, uint16_t &out);
float diodeTemperature(uint8_t msb, uint8_t lsb);
uint16_t fanRpm(uint16_t tach);
// TPS546 STATUS_WORD bits that stop the ASIC, as ESP-Miner: OFF (while
// powered), VOUT_OV, IOUT_OC, VIN_UV and TEMP. The other bits also report
// warnings, and some stay latched (OTHER), while the measured limits below
// already cover what they would tell.
constexpr float kAsicMaxC = 70;  // ASIC diode: the hard stop
constexpr uint16_t kStatusOff = 0x0040, kStatusVinUv = 0x0008;
constexpr uint16_t kStatusFaults = 0x0020 | 0x0010 | 0x0004;  // VOUT_OV, IOUT_OC, TEMP
struct Health {
    float temperature, regulatorTemperature, inputVoltage, coreVoltage, current;
    uint16_t rpm, status;
    // A fault stops the ASIC until restart (null: none).
    const char *fault(bool powered, bool requireDiode = true) const;
    // Before power-on: why the ASIC cannot start yet (null: ready). Waiting,
    // not a fault: a Gamma on USB only has neither 5 V nor a turning fan.
    const char *notReady() const;
};
struct Counter {
    uint32_t previous = 0, at = 0;
    bool ready = false;
    void reset() { ready = false; }
    uint64_t update(uint32_t value, uint32_t now);
};
} // namespace bm1370
