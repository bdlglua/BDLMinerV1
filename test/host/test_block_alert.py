#!/usr/bin/env python3
"""Execute the production block alert offline, including the very first tick."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/watch.cpp').read_text()
alert = source[source.index('void blockAlert('):source.index('void events(')]
prefix = r'''
#include "miner.h"
#include "core/coin.h"
#include <cassert>
#include <cstdio>
BlockCandidate latest{};
bool haveCandidate = false;
bool minerLastBlockCandidate(BlockCandidate &out) { out = latest; return haveCandidate; }
struct { bool beep = true; } settings;
int notifications = 0, wakeups = 0, beeps = 0;
uint32_t shownHeight = 0;
std::string title, message;
void notify(const char *t, const char *m, int priority, const char *) {
    assert(priority == 5); ++notifications; title = t; message = m;
}
void logf(const char *, ...) {}
void watchWake() { ++wakeups; }
namespace board { const char *modelName() { return "Test"; } void beep() { ++beeps; } }
namespace ui { void blockFound(uint32_t h) { shownHeight = h; } }
'''
tests = r'''
int main() {
    // A candidate is already present before the very first watch tick.
    latest.number = 1; latest.height = 100; haveCandidate = true;
    blockAlert();
    assert(notifications == 1 && wakeups == 1 && beeps == 1 && shownHeight == 100);
    assert(title == "BLOCK CANDIDATE" && message.find("height 100") != std::string::npos);
    assert(message.find("chain confirmation") != std::string::npos);
    blockAlert(); assert(notifications == 1);
    settings.beep = false; latest.number = 2; latest.height = 101;
    blockAlert();
    assert(notifications == 2 && wakeups == 2 && beeps == 1 && shownHeight == 101);
    latest.number = 3; latest.previousSession = true; blockAlert();
    assert(notifications == 2 && wakeups == 2); // no celebration on reboot
    puts("Block alert: startup candidate, own proof height, no duplicate alert, wake and optional beep passed");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    cpp = Path(temporary) / 'test.cpp'
    binary = Path(temporary) / 'test'
    cpp.write_text(prefix + alert + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Itest/host/stubs/miner', '-Isrc', str(cpp), '-o', str(binary)], cwd=root, check=True)
    subprocess.run([str(binary)], check=True)
