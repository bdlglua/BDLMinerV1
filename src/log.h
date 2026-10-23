// Log lines go to the serial port and to a ring buffer shown by the web
// dashboard (/api/log), so the miner can be diagnosed without a cable.
#pragma once

#include <Arduino.h>

void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// The buffered lines, oldest first (the last few KB).
String logText();

// Records why the board started (reset reason, plus the reason the miner gave
// when it restarted itself) and logs it. Call once at boot.
void logBoot();
const char *bootReason();

// Saves the reason for a restart the miner is about to do itself.
void setRestartReason(const char *why);
