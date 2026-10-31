// Portable thermal policy and expiring permission to mine. The expiry is
// checked by each mining task, even while the main loop is blocked by HTTP.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace mining {

constexpr uint32_t kSafetyTimeoutMs = 3000;
constexpr uint32_t kFixedFilter = 0xFFFF;
inline bool fixedFilterSupports(uint32_t limit) { return limit <= kFixedFilter; }

enum class TemperatureSource : uint8_t { Unknown, Imu, Chip };
struct Thermometer {
    TemperatureSource source = TemperatureSource::Unknown;
    float sample(float imu, float chip) {
        if (source == TemperatureSource::Unknown) {
            if (std::isfinite(imu)) source = TemperatureSource::Imu;
            else if (std::isfinite(chip)) source = TemperatureSource::Chip;
        }
        // Keep one physical sensor for the session. Falling back from a hot
        // IMU to a cooler chip sensor must not pretend the board cooled down.
        float t = source == TemperatureSource::Imu ? imu : chip;
        return std::isfinite(t) ? t : NAN;
    }
};

struct ThermalPolicy {
    bool hot = false;
    bool sensorSeen = false;
    bool sensorFault = false;
    // ASIC boards: no 50% / 25% steps below the limit (each power change
    // restarts the ASIC, which then cools at once: a Gamma 601 at 600 MHz
    // restarted every 40 s at 55 C), and once hot, a pause of at least
    // holdMs (the ASIC's own sensor is silent while it is off).
    bool steps = true;
    uint32_t holdMs = 0, hotAt = 0;

    uint8_t power(float temperature, uint8_t limit, uint8_t requested, bool batterySaver, uint32_t now = 0) {
        uint8_t result = requested > 100 ? 100 : requested;
        if (batterySaver && result > 50) result = 50;
        sensorFault = limit && !std::isfinite(temperature);
        if (!limit) {
            hot = false;
        } else if (sensorFault) {
            // A missing sensor on a classic ESP32 permits only a conservative
            // duty cycle. Losing a previously working sensor stops mining.
            if (hot || sensorSeen) return 0;
            if (result > 25) result = 25;
        } else {
            sensorSeen = true;
            if (temperature >= limit) {
                if (!hot) hotAt = now;
                hot = true;
            } else if (temperature <= limit - 5 && (!holdMs || now - hotAt >= holdMs)) {
                hot = false;
            }
            if (hot) return 0;
            if (!steps) return result;
            if (temperature >= limit - 5 && result > 25) result = 25;
            else if (temperature >= limit - 10 && result > 50) result = 50;
        }
        return result;
    }
};

class SafetyLease {
    std::atomic<uint32_t> sampledAt{0};
    std::atomic<uint32_t> requested{0};
    std::atomic<bool> ready{false};

public:
    void update(uint32_t now, uint8_t power) {
        requested.store(power > 100 ? 100 : power, std::memory_order_relaxed);
        sampledAt.store(now, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
    }
    uint8_t power(uint32_t now) const {
        if (!ready.load(std::memory_order_acquire) ||
            now - sampledAt.load(std::memory_order_relaxed) > kSafetyTimeoutMs) return 0;
        return requested.load(std::memory_order_relaxed);
    }
};

inline uint32_t restMs(uint32_t busyMs, uint8_t power) {
    if (!power || power >= 100) return 0;
    return (uint64_t(busyMs ? busyMs : 1) * (100 - power) + power - 1) / power;
}

}  // namespace mining
