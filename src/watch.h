// Once-a-second supervision from the main loop: heat and battery throttling,
// night mode, notifications, restarts when stuck, share flashes, automatic
// screen changes and rotation. Mining permission expires independently in
// each worker if this loop cannot refresh it for three seconds.
#pragma once

void watchTick();

// Night mode (settings.nightFrom..nightTo): screen off and no beep.
bool watchNight();
// Cached reason for a reduced duty cycle; no sensor/I2C access from HTTP.
const char *watchLimitReason();

// A button was pressed: lights the screen for 30 s even at night.
void watchWake();
