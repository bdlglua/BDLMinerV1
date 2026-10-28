// The core dump the ESP-IDF panic handler writes to the "coredump" partition
// after a crash: summarised at boot in the log and on the dashboard, and
// downloadable whole (an ELF readable with esp-coredump / GDB).
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void crashBegin();            // at boot, after logBoot()
bool crashPresent();
const String &crashSummary();  // "task loopTask, PC 0x400d1234, ...", empty without a dump
size_t crashSize();
// Copies `length` bytes of the dump from `offset`; false past its end or on a read error.
bool crashRead(size_t offset, uint8_t *out, size_t length);
// Erases the report and the history.
bool crashErase();
// Dates the newest history entry once the clock is set; call once a second.
void crashPoll();
// The last 5 crashes, newest first: {t (Unix time, 0 unknown), version, summary}.
void crashHistory(JsonArray out);
bool crashHasHistory();
