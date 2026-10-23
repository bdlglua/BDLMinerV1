// Once-a-second supervision from the main loop: heat and battery throttling,
// night mode, notifications, restarts when stuck, share flashes, automatic
// screen changes and rotation.
#pragma once

void watchTick();

// Night mode (settings.nightFrom..nightTo): screen off and no beep.
bool watchNight();

// A button was pressed: lights the screen for 30 s even at night.
void watchWake();
