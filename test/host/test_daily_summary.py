#!/usr/bin/env python3
"""Execute the production daily summary offline: a skipped summary starts a new period."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/watch.cpp').read_text()
summary = source[source.index('String rateText('):source.index('void screens(')]
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
using String = std::string;
struct MinerStats { uint32_t accepted = 0, rejected = 0, blocksFound = 0; double bestDiff = 0; };
struct PoolUptime { uint32_t connectedMs = 0, sessions = 0; };
struct Preferences {
    bool begin(const char *, bool) { return true; }
    uint32_t getUInt(const char *, uint32_t fallback) { return fallback; }
    void putUInt(const char *, uint32_t) {}
    void end() {}
};
struct { uint8_t summaryHour = 20; } settings;
struct tm clock_{};
bool clockNow(struct tm &out) { out = clock_; return true; }
double periodBest = 0;
double minerTakePeriodBest() { double best = periodBest; periodBest = 0; return best; }
void found(double difficulty) { if (difficulty > periodBest) periodBest = difficulty; }  // as onCandidate()
uint64_t minerTotalHashes() { return 0; }
PoolUptime minerPoolUptime() { return {}; }
int sent = 0;
std::string message;
void notify(const char *, const char *m, int, const char *) { ++sent; message = m; }
void logf(const char *, ...) {}
'''
tests = r'''
int main() {
    MinerStats stats;
    clock_.tm_year = 126; clock_.tm_hour = 3;
    summary(0, stats);  // first call: the period starts at boot
    // Ten minutes after boot is the summary hour: too early, nothing is sent.
    found(1000); clock_.tm_hour = 20; clock_.tm_yday = 1;
    summary(10 * 60 * 1000, stats);
    assert(sent == 0);
    // The next day's summary covers only its own period: the 1000 found in the
    // skipped one must not be reported as that day's best.
    found(7); clock_.tm_yday = 2;
    summary(10 * 60 * 1000 + 24 * 3600000u, stats);
    assert(sent == 1 && message.find("best 7 ") != std::string::npos);
    puts("Daily summary: a skipped summary also starts a new best-difficulty period passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-summary-') as tmp:
    test = Path(tmp) / 'test.cpp'
    binary = Path(tmp) / 'test'
    test.write_text(prefix + summary + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
