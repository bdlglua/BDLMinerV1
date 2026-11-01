// Gamma fan policy. Full cooling always takes priority over user settings.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mining {
constexpr uint8_t kFanMinPercent = 30;
inline uint8_t fanPercent(unsigned value) {
    return uint8_t(std::max<unsigned>(kFanMinPercent, std::min<unsigned>(100, value)));
}
// EMC2101 direct setting: 0..63. Round up so the minimum is never undershot.
inline uint8_t fanPwm(unsigned percent) { return (fanPercent(percent) * 63u + 99u) / 100u; }
inline uint8_t fanPwmPercent(uint8_t pwm) { return (unsigned(pwm) * 100u + 31u) / 63u; }
struct FanDecision { uint8_t percent; bool forced; const char *reason; };

class FanControl {
    bool hot = false;
    uint8_t current = 100;
    uint32_t changedAt = 0;
    FanDecision full(const char *reason, uint32_t now) {
        current = 100; changedAt = now;
        return {100, true, reason};
    }
public:
    FanDecision update(bool running, bool automatic, unsigned requested,
                       float asicC, float regulatorC, uint32_t now, unsigned thermalLimit = 65) {
        if (!running) return full("Startup or stopped ASIC", now);
        if (!std::isfinite(asicC) || asicC < 0 || !std::isfinite(regulatorC) || regulatorC < -20)
            return full("Temperature sensor unavailable", now);
        float fullAt = thermalLimit >= 30 ? float(std::min(60u, thermalLimit - 5)) : 60.0f;
        if (asicC >= fullAt || regulatorC >= 80) hot = true;
        else if (asicC <= fullAt - 5 && regulatorC <= 75) hot = false;
        if (hot) return full("Temperature safety", now);
        unsigned target = fanPercent(requested);
        if (automatic) {
            // Default curve: 30% at <=40 C, full at 60 C (regulator: 60..80 C).
            // A lower user thermal limit moves the ASIC curve down with it.
            float distance = std::max(asicC - fullAt, regulatorC - 80);
            target = fanPercent(unsigned(std::max(30.0f, std::ceil(100 + distance * 3.5f))));
        }
        // Raise cooling immediately. Automatic reductions wait at least 5 s
        // after the previous change; manual changes apply on the next sample.
        if (!automatic || target >= current || uint32_t(now - changedAt) >= 5000) {
            if (current != target) changedAt = now;
            current = target;
        }
        return {current, false, automatic ? "Automatic" : "Manual"};
    }
};
} // namespace mining
