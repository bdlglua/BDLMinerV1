#!/usr/bin/env python3
"""Fault-inject the production firmware confirmation and retry paths."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/trial.cpp').read_text()
state = source[source.index('namespace {'):source.index('void trialBegin()')]
poll = source[source.index('void trialPoll()'):source.index('void trialExpect(')]
prefix = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "trial.h"
#define FW_VERSION "test"
constexpr int ESP_OK = 0;
int confirmCalls = 0, removed = 0, rollbacks = 0;
bool failConfirmation = true;
uint32_t clockMs = 181000;
uint32_t millis() { return clockMs; }
void delay(unsigned) {}
void logf(const char *, ...) {}
void minerSaveStats() {}
void setRestartReason(const char *) {}
double minerHashrate() { return 1000000; }
unsigned minerPowerPercent() { return 100; }
struct Stats { bool poolConnected = true; };
Stats minerStats() { return {}; }
struct { bool isConnected() { return true; } } WiFi;
struct Preferences {
    bool begin(const char *, bool) { return true; }
    void remove(const char *) { ++removed; }
    void end() {}
};
int esp_ota_mark_app_valid_cancel_rollback() { ++confirmCalls; return failConfirmation ? -1 : ESP_OK; }
void esp_ota_mark_app_invalid_rollback_and_reboot() { ++rollbacks; }
'''
tests = r'''
int main() {
    pending = true;
    trialConfirm("manual request");
    assert(trialPending() && confirmCalls == 1 && removed == 0);
    // The production poll must retry a failed flash confirmation, while
    // preserving the trial record and the truthful dashboard state.
    trialPoll();
    assert(trialPending() && confirmCalls == 2 && removed == 0);
    failConfirmation = false;
    trialPoll();
    assert(!trialPending() && confirmCalls == 3 && removed == 5 && rollbacks == 0);
    trialPoll(); trialConfirm("already confirmed");
    assert(confirmCalls == 3 && removed == 5);
    puts("Firmware trial: failed confirmation stays pending, records retained, poll retries, success confirms once");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-trial-') as temp:
    cpp = Path(temp) / 'test.cpp'
    exe = Path(temp) / 'test'
    cpp.write_text(prefix + state + poll + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Isrc', str(cpp), '-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], check=True)
