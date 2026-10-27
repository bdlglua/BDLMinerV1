// Native Bitaxe Gamma 601 backend (experimental, one BM1370).
#pragma once
#ifdef BOARD_GAMMA601
#include <cstdint>
namespace gamma601 {
struct Status {
    float temperature, regulatorTemperature, inputVoltage, coreVoltage, current, frequency;
    uint16_t rpm, regulatorStatus;
    bool running;
    const char *state;
};
// Call with reset already asserted, after the shared LGFX I2C bus is initialized.
void begin();
bool start();
Status status();
float temperature();
}
#endif
