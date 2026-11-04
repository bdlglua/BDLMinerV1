#!/usr/bin/env python3
"""Execute the production screen durations: truncated units, never "1d 24h"."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/ui.cpp').read_text()
duration = source[source.index('String duration('):source.index('String priceText(')]
short = source[source.index('String shortDuration('):source.index('// metrics[0] is the page')]
prefix = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
using namespace std;
using String = std::string;
const char *tr(const char *text) { return text; }
namespace work { std::string formatSI(double v, int) { char b[32]; snprintf(b, sizeof(b), "%.1f", v); return b; } }
'''
tests = r'''
int main() {
    assert(duration(59.6) == "59s" && duration(3599) == "59min" && duration(3600) == "1h00");
    assert(duration(47.6 * 3600) == "1d 23h");   // was "1d 24h"
    assert(duration(48 * 3600) == "2d 0h" && duration(-1) == "-");
    assert(shortDuration(3599) == "59min" && shortDuration(47.6 * 3600) == "1d 23h");
    puts("Screen durations: truncated seconds, minutes and hours, never 60min or 24h passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-ui-') as tmp:
    test = Path(tmp) / 'test.cpp'
    binary = Path(tmp) / 'test'
    test.write_text(prefix + duration + short + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wno-format-security', str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
