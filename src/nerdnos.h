// Native NerdNOS backend (experimental): one BM1397 on a board that carries a
// LilyGO T-Display-S3. Pins and start-up from the NerdNOS firmware
// (WantClue/NerdMiner_v2, nerdnos branch); see docs/hardware/nerdnos.md.
#pragma once
#ifdef BOARD_NERDNOS
#include <cstdint>
namespace nerdnos {
struct Status {
    float temperature, frequency;
    bool running;
    const char *state;
    float hardwareErrorPercent;  // -1 until enough results
    uint32_t results;            // verified ASIC results since boot
};
void begin();  // ASIC unpowered and held in reset until there is work
bool start();
Status status();
// NTC next to the ASIC; NAN without a usable reading. Main loop only (ADC).
float temperature();
}  // namespace nerdnos
#endif
