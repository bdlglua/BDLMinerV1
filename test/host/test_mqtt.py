#!/usr/bin/env python3
"""Compile the production MQTT percentage parser and reject partial numbers."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/mqtt.cpp').read_text()
parser = source[source.index('bool parsePercent('):source.index('void onCommand(')]
tests = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
PARSER
int main() {
    for (int expected = 0; expected <= 100; ++expected) {
        char text[8]; std::snprintf(text, sizeof(text), "%d", expected);
        int result = -1; assert(parsePercent(text, std::strlen(text), result) && result == expected);
    }
    for (const char *text : {"", "-oops", "50oops", "1.5", "50%", "1e2", "-1", "101",
                            "99999999999999999999999999", " 50", "50 ", "+50", "nan"}) {
        int result = -1; assert(!parsePercent(text, std::strlen(text), result) && result == -1);
    }
    int result = -1; assert(!parsePercent(nullptr, 5, result) && result == -1);
    assert(!parsePercent("50\0oops", 7, result) && result == -1);
    assert(parsePercent("00075", 5, result) && result == 75);
    puts("MQTT: complete 0-100 percentages accepted; malformed, fractional and overflowing commands refused");
}
'''.replace('PARSER', parser)
with tempfile.TemporaryDirectory(prefix='bdl-mqtt-') as temp:
    cpp, exe = Path(temp) / 'test.cpp', Path(temp) / 'test'
    cpp.write_text('#include <initializer_list>\n' + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
