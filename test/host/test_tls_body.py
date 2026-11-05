#!/usr/bin/env python3
"""TlsBodyReader: a body spread over TLS records with gaps between them is read whole."""
import pathlib, subprocess, tempfile

root = pathlib.Path(__file__).resolve().parents[2]
header = (root / 'src/tls.h').read_text()
reader = header[header.index('class TlsBodyReader'):header.index('};', header.index('class TlsBodyReader')) + 2]
body = (root / 'src/tls_body.cpp').read_text().replace('#include "tls.h"\n', '')

prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
static uint32_t now = 0;
static uint32_t millis() { return now; }
static void vTaskDelay(uint32_t) { now += 1; }
// A TLS client whose records arrive one by one: between them read() finds
// nothing, as NetworkClientSecure does while the next record is on its way.
struct WiFiClientSecure {
    std::deque<std::pair<uint32_t, std::string>> records;  // arrival time, bytes
    std::string ready;
    bool open = true;
    void arrive() {
        while (!records.empty() && records.front().first <= now) {
            ready += records.front().second;
            records.pop_front();
        }
    }
    int available() { arrive(); return (int)ready.size(); }
    int read(uint8_t *buf, size_t size) {
        arrive();
        if (ready.empty()) return -1;
        size_t n = size < ready.size() ? size : ready.size();
        memcpy(buf, ready.data(), n);
        ready.erase(0, n);
        return (int)n;
    }
    bool connected() { arrive(); return open || !ready.empty() || !records.empty(); }
};
'''

tests = r'''
int main() {
    {   // three records, 400 ms apart: read whole
        WiFiClientSecure c; c.records = {{0, "{\"a\":"}, {400, "[1,2,"}, {800, "3]}"}};
        TlsBodyReader r(c, 1000);
        char buf[32] = {0};
        assert(r.readBytes(buf, 13) == 13 && std::string(buf) == "{\"a\":[1,2,3]}");
    }
    {   // byte by byte, as ArduinoJson reads: same result
        WiFiClientSecure c; c.records = {{0, "ab"}, {300, "cd"}};
        TlsBodyReader r(c, 1000);
        std::string s;
        for (int i = 0; i < 4; i++) { int ch = r.read(); assert(ch >= 0); s += (char)ch; }
        assert(s == "abcd");
    }
    {   // no data for longer than the timeout: stops with what it has
        WiFiClientSecure c; c.records = {{0, "ab"}, {5000, "cd"}};
        TlsBodyReader r(c, 1000);
        char buf[8] = {0};
        assert(r.readBytes(buf, 4) == 2);
    }
    {   // connection closed by the server: stops at once, without waiting
        WiFiClientSecure c; c.records = {{0, "ab"}}; c.open = false;
        TlsBodyReader r(c, 10000);
        char buf[8] = {0};
        uint32_t t0 = now;
        assert(r.readBytes(buf, 4) == 2 && now - t0 < 5);
        assert(r.read() == -1);
    }
    puts("TLS body reader: records with gaps read whole, timeout and closed connection stop it passed");
}
'''

# size_t readBytes(uint8_t*) overload in the header needs no change for the host build.
with tempfile.TemporaryDirectory(prefix='bdl-tls-body-') as d:
    cpp = pathlib.Path(d) / 'test.cpp'
    cpp.write_text(prefix + reader + body + tests)
    exe = pathlib.Path(d) / 'test'
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
