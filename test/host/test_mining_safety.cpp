#include <cassert>
#include <cmath>
#include <cstdio>
#include <thread>

#include "core/mining_safety.h"
#include "ota.h"

int main() {
    mining::Thermometer thermometer;
    assert(thermometer.sample(70, 40) == 70);
    assert(thermometer.source == mining::TemperatureSource::Imu);
    assert(std::isnan(thermometer.sample(NAN, 40))); // no silent sensor switch
    assert(thermometer.sample(55, 40) == 55);
    mining::Thermometer chip;
    assert(std::isnan(chip.sample(NAN, NAN)));
    assert(chip.sample(NAN, 40) == 40);
    assert(chip.sample(80, 42) == 42); // still the originally chosen chip sensor
    assert(std::isnan(chip.sample(80, NAN)));

    mining::ThermalPolicy thermal;
    assert(thermal.power(45, 65, 75, false) == 75);  // USB
    assert(thermal.power(45, 65, 75, true) == 50);   // battery
    assert(thermal.power(55, 65, 75, false) == 50);
    assert(thermal.power(60, 65, 75, false) == 25);
    assert(thermal.power(65, 65, 75, false) == 0);
    assert(thermal.hot);
    assert(thermal.power(NAN, 65, 75, false) == 0);
    assert(thermal.hot && thermal.sensorFault);
    assert(thermal.power(61, 65, 75, false) == 0);  // hysteresis
    assert(thermal.power(60, 65, 75, false) == 25);
    assert(!thermal.hot && !thermal.sensorFault);
    assert(thermal.power(INFINITY, 65, 75, false) == 0);
    assert(thermal.power(45, 65, 75, false) == 75);
    assert(thermal.power(NAN, 0, 75, false) == 75); // explicit disable

    mining::ThermalPolicy missing;
    assert(missing.power(NAN, 65, 75, false) == 25);
    assert(missing.sensorFault && !missing.sensorSeen);
    assert(missing.power(40, 65, 75, false) == 75);
    assert(missing.power(NAN, 65, 75, false) == 0);
    assert(missing.power(45, 65, 10, false) == 10);
    assert(missing.power(45, 65, 0, false) == 0);

    mining::SafetyLease lease;
    assert(lease.power(1000) == 0);  // no first sample yet
    lease.update(1000, 75);
    assert(lease.power(4000) == 75);
    assert(lease.power(4001) == 0);  // HTTP/main loop stopped, miners still run
    lease.update(5000, 0);
    assert(lease.power(5000) == 0);
    lease.update(UINT32_MAX - 1000, 100);
    assert(lease.power(500) == 100);
    assert(lease.power(2500) == 0); // timeout still works across millis wrap
    assert(mining::restMs(100, 75) == 34);
    assert(mining::restMs(100, 50) == 100);
    assert(mining::restMs(100, 25) == 300);
    assert(mining::restMs(100, 100) == 0);
    assert(mining::restMs(100, 0) == 0);

    assert(mining::fixedFilterSupports(0xFFFF));
    assert(!mining::fixedFilterSupports(0x10000));

    assert(!ota::acquire(ota::Owner::None));
    {
        ota::Guard github(ota::Owner::Github);
        assert(github && ota::busy());
        ota::Guard web(ota::Owner::Web);
        assert(!web);
        ota::release(ota::Owner::Web); // cannot release somebody else's session
        assert(ota::owns(ota::Owner::Github));
    }
    assert(!ota::busy());
    // The two OTA entry points really race on the production ownership code.
    for (int i = 0; i < 64; ++i) {
        std::atomic<int> ready{0}, attempted{0}, winners{0};
        auto race = [&](ota::Owner who) {
            ++ready;
            while (ready != 2) std::this_thread::yield();
            bool got = ota::acquire(who);
            if (got) ++winners;
            ++attempted;
            while (attempted != 2) std::this_thread::yield();
            if (got) {
                assert(ota::owns(who));
                ota::release(who);
            }
        };
        std::thread web(race, ota::Owner::Web), github(race, ota::Owner::Github);
        web.join();
        github.join();
        assert(winners == 1 && !ota::busy());
    }
    puts("Thermal policy, stalled supervision, duty cycle and concurrent OTA: passed");
}
