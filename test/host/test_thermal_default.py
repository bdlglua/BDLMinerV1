#!/usr/bin/env python3
"""2.5.3: chip-sensor boards still at the former 65 C default are raised once to 75 C."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/settings.cpp').read_text()
function = source[source.index('static void raiseChipThermalDefaultOnce()'):source.index('// From 1.9.3 the dashboard')]
header = (root / 'src/settings.h').read_text()
constants = header[header.index('constexpr uint8_t kChipSensorMaxTemp'):].split('\n', 1)[0]
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
''' + constants + r'''
struct Settings { uint8_t maxTemp = kChipSensorMaxTemp; } settings;
static const char *kNamespace = "bdlminer";
std::map<std::string, bool> stored;      // the NVS keys of the namespace
bool saveWorks = true;
int saved = -1;
bool settingsSave(const Settings &s) { if (saveWorks) saved = s.maxTemp; return saveWorks; }
void logf(const char *, ...) {}
struct Preferences {
    bool writable = false;
    bool begin(const char *, bool readOnly) { writable = !readOnly; return true; }
    bool isKey(const char *k) { return stored.count(k); }
    void putBool(const char *k, bool v) { assert(writable); stored[k] = v; }
    void end() {}
};
'''
tests = r'''
int main() {
    assert(kChipSensorMaxTemp == 75 && kOldMaxTempDefault == 65);
    // The former default is raised, once, and saved.
    settings.maxTemp = 65; raiseChipThermalDefaultOnce();
    assert(settings.maxTemp == 75 && saved == 75 && stored.count("tmp75"));
    // Chosen again afterwards, 65 stays.
    settings.maxTemp = 65; saved = -1; raiseChipThermalDefaultOnce();
    assert(settings.maxTemp == 65 && saved == -1);
    // Any other saved choice, 0 (off) included, is kept, and marked done.
    for (int choice : {0, 50, 60, 70, 80}) {
        stored.clear(); saved = -1; settings.maxTemp = choice; raiseChipThermalDefaultOnce();
        assert(settings.maxTemp == choice && saved == -1 && stored.count("tmp75"));
    }
    // A failed save keeps 65 and tries again at the next start.
    stored.clear(); saveWorks = false; settings.maxTemp = 65; raiseChipThermalDefaultOnce();
    assert(settings.maxTemp == 65 && !stored.count("tmp75"));
    saveWorks = true; raiseChipThermalDefaultOnce();
    assert(settings.maxTemp == 75 && stored.count("tmp75"));
    puts("Chip-sensor thermal default raised once from 65 to 75 C, other choices kept: passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'thermal_default.cpp'
    src.write_text(prefix + function + tests)
    exe = Path(tmp) / 'thermal_default'
    subprocess.run(['g++', '-std=c++17', '-Wall', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
