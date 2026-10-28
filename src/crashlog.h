// The core dump the ESP-IDF panic handler writes to the "coredump" partition
// after a crash: summarised at boot in the log and on the dashboard, and
// downloadable whole (an ELF readable with esp-coredump / GDB).
#pragma once

#include <Arduino.h>

void crashBegin();            // at boot, after logBoot()
bool crashPresent();
const String &crashSummary();  // "task loopTask, PC 0x400d1234, ...", empty without a dump
size_t crashSize();
// Copies `length` bytes of the dump from `offset`; false past its end or on a read error.
bool crashRead(size_t offset, uint8_t *out, size_t length);
bool crashErase();
