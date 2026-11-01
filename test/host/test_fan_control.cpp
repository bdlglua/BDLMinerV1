#include "core/fan_control.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    mining::FanControl fan;
    auto d = fan.update(false, false, 30, 40, 50, 1000);
    assert(d.percent == 100 && d.forced);
    d = fan.update(true, false, 30, 50, 50, 1001);
    assert(d.percent == 30 && !d.forced && !strcmp(d.reason, "Manual"));
    assert(fan.update(true, false, 30, 60, 50, 1002).percent == 100);
    assert(fan.update(true, false, 30, 58, 50, 1003).forced); // hysteresis
    assert(fan.update(true, false, 30, 55, 75, 1004).percent == 30);
    assert(fan.update(true, false, 30, 50, 80, 1005).forced);
    assert(fan.update(true, false, 30, 50, 76, 1006).forced);
    assert(!fan.update(true, false, 30, 50, 75, 1007).forced);
    for (float bad : {NAN, INFINITY, -1.0f})
        assert(fan.update(true, false, 30, bad, 50, 1008).percent == 100);
    assert(fan.update(true, false, 30, 50, NAN, 1009).forced);
    for (unsigned percent = 0; percent < 256; ++percent) {
        unsigned bounded = std::max(30u, std::min(100u, percent));
        assert(fan.update(true, false, percent, 50, 50, 2000).percent == bounded);
        auto pwm = mining::fanPwm(percent);
        assert(pwm >= 19 && pwm <= 63 && pwm * 100u >= bounded * 63u);
        assert(mining::fanPwmPercent(pwm) >= 30 && mining::fanPwmPercent(pwm) <= 100);
    }
    fan = mining::FanControl();
    fan.update(false, true, 100, 40, 50, 1000);
    assert(fan.update(true, true, 100, 40, 50, 5999).percent == 100);
    assert(fan.update(true, true, 100, 40, 50, 6000).percent == 30);
    assert(fan.update(true, true, 100, 50, 50, 6001).percent == 65); // raise immediately
    assert(fan.update(true, true, 100, 45, 50, 6002).percent == 65); // lower slowly
    assert(fan.update(true, true, 100, 45, 50, 11001).percent == 48);
    assert(fan.update(true, true, 100, 40, 75, 11002).percent == 83); // regulator also controls cooling
    fan.update(false, true, 100, 40, 50, UINT32_MAX - 1000);
    assert(fan.update(true, true, 100, 40, 50, 4000).percent == 30); // millis wrap
    mining::FanControl custom;
    assert(custom.update(true, false, 30, 50, 50, 1000, 55).forced); // cooler configured limit
    assert(!custom.update(true, false, 30, 45, 50, 1001, 55).forced);
    assert(custom.update(true, false, 30, 50, 50, 1002, 0).percent == 30); // off user limit retains hard ceiling
    assert(custom.update(true, false, 30, 60, 50, 1003, 0).forced);
    puts("Gamma fan policy: minimum/PWM bounds, startup, both sensors, thermal hysteresis, automatic pacing and timer wrap passed");
}
