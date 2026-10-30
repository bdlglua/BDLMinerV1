#!/usr/bin/env python3
"""From 1.9.3 a web password saved by an earlier version is removed once."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/settings.cpp').read_text()
function = source[source.index('static void openDashboardOnce()'):source.index('void settingsLoad()')]
prefix = r'''
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
struct String : std::string {
    using std::string::string;
    bool isEmpty() const { return empty(); }
};
struct Settings { String webPassword; } settings;
static const char *kNamespace = "bdlminer";
std::map<std::string, bool> stored;      // the NVS keys of the namespace
bool saveWorks = true;
String saved = "unchanged";
bool settingsSave(const Settings &s) { if (saveWorks) saved = s.webPassword; return saveWorks; }
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
    // An earlier version's password goes, once.
    settings.webPassword = "secret"; openDashboardOnce();
    assert(settings.webPassword.isEmpty() && saved.empty() && stored.count("pwopen"));
    // One set afterwards stays.
    settings.webPassword = "new"; saved = "unchanged"; openDashboardOnce();
    assert(settings.webPassword == "new" && saved == "unchanged");
    // A failed save keeps the password and tries again at the next start.
    stored.clear(); saveWorks = false; settings.webPassword = "secret"; openDashboardOnce();
    assert(settings.webPassword == "secret" && !stored.count("pwopen"));
    saveWorks = true; openDashboardOnce();
    assert(settings.webPassword.isEmpty() && stored.count("pwopen"));
    // No password: nothing to save, marked done.
    stored.clear(); saved = "unchanged"; openDashboardOnce();
    assert(saved == "unchanged" && stored.count("pwopen"));
    puts("Web password from earlier versions removed once, later ones kept: passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'open_dashboard.cpp'
    src.write_text(prefix + function + tests)
    exe = Path(tmp) / 'open_dashboard'
    subprocess.run(['g++', '-std=c++17', '-Wall', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
