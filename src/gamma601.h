// Native Bitaxe Gamma 601 backend (experimental, one BM1370).
#pragma once
#include <stddef.h>
#ifdef BOARD_GAMMA601
#include <cstdint>
namespace gamma601 {
struct Status {
    float temperature, regulatorTemperature, inputVoltage, coreVoltage, current, frequency;
    uint16_t rpm, regulatorStatus;
    bool running;
    const char *state;
    uint16_t requestedMHz;
    bool stockFallback;
    const char *fallbackReason;
    float hardwareErrorPercent; // -1 until a complete measurement window
};
// Call with reset already asserted, after the shared LGFX I2C bus is initialized.
void begin();
bool start();
Status status();
float temperature();
// Where temperature() comes from: "asic" (its diode), "board" (EMC2101, while
// the ASIC is off and its diode silent) or "unavailable".
const char *temperatureSource();
// The last fault that stopped the ASIC, with its readings ("" if none), kept
// across restarts; returns its Unix time (0 if the clock was not set).
uint32_t lastFault(char *out, size_t size);
}
#endif
