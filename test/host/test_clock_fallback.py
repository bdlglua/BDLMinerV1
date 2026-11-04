#!/usr/bin/env python3
"""Execute the production clock fallback: the pool's job time when NTP is blocked."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/market.cpp').read_text()
fallback = source[source.index('// Some networks block NTP.'):source.index('void marketTask(')]
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ctime>
uint32_t uptime = 0, jobTime = 0;
time_t clock_ = 0;
int sets = 0;
bool settable = true;
uint32_t millis() { return uptime; }
uint32_t minerJobTime() { return jobTime; }
bool tlsClockSet() { return clock_ >= 1700000000; }
int settimeofday(const timeval *tv, const void *) { if (!settable) return -1; clock_ = tv->tv_sec; ++sets; return 0; }
void logf(const char *, ...) {}
'''
tests = r'''
int main() {
    // NTP gets its first minute; no job yet: nothing to set the clock from.
    uptime = 30000; jobTime = 1800000000; clockFromPool(); assert(sets == 0);
    uptime = 61000; jobTime = 0; clockFromPool(); assert(sets == 0);
    // NTP answered meanwhile: its time stands.
    clock_ = 1800000500; jobTime = 1800000000; clockFromPool(); assert(sets == 0 && clock_ == 1800000500);
    // NTP blocked: the pool's job time, once.
    clock_ = 5; settable = false; clockFromPool(); assert(sets == 0);
    settable = true; clockFromPool(); assert(sets == 1 && clock_ == 1800000000);
    clock_ = 5; jobTime = 1800000900; clockFromPool(); assert(sets == 1);
    puts("Clock fallback: NTP first, then the pool's job time once, never over a set clock passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-clock-') as tmp:
    test = Path(tmp) / 'test.cpp'
    binary = Path(tmp) / 'test'
    test.write_text(prefix + fallback + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
