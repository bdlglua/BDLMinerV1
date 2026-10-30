#!/usr/bin/env python3
"""Anonymous hashrate sharing: the report's content and its schedule."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/share.cpp').read_text()
body = source[source.index('namespace {'):]
prefix = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define FW_VERSION "1.9.0"
#define FW_ENV "m5stickc-plus2"
#define STATS_URL "https://stats.example/report"
using std::isfinite;
struct String : std::string {
    using std::string::string;
    String(const std::string &s) : std::string(s) {}
    String(int n) : std::string(std::to_string(n)) {}
    int indexOf(const char *s, int from = 0) const { auto i = find(s, from); return i == npos ? -1 : (int)i; }
    int indexOf(char c, int from = 0) const { auto i = find(c, from); return i == npos ? -1 : (int)i; }
    String substring(int a, int b) const { return substr(a, b - a); }
    size_t length() const { return size(); }
};
void logf(const char *, ...) {}
uint32_t nowMs = 0;
uint32_t millis() { return nowMs; }
struct { bool connected = true; bool isConnected() const { return connected; } } WiFi;
bool memoryOk = true;
bool tlsMemoryOk() { return memoryOk; }
double rate = 1.0234e6;
double minerHashrate() { return rate; }
struct { bool shareHashrate = true; } settings;
struct WiFiClientSecure {};
std::vector<std::string> posted, hosts;
int answer = 204;
bool tlsConnect(WiFiClientSecure &, const char *host, uint16_t = 443, uint8_t = 12) { hosts.push_back(host); return true; }
struct HTTPClient {
    std::string url;
    void setTimeout(int) {}
    bool begin(WiFiClientSecure &, const String &u) { url = u; return true; }
    void setUserAgent(const char *) {}
    void addHeader(const char *, const char *) {}
    int POST(uint8_t *data, size_t n) { posted.emplace_back((char *)data, n); return answer; }
    void end() {}
};
size_t shareBody(char *out, size_t size, double hashrate);
void sharePoll();
'''
tests = r'''
int main() {
    char out[128];
    assert(shareBody(out, sizeof(out), 1.0234e6));
    assert(!strcmp(out, R"({"v":"1.9.0","board":"m5stickc-plus2","hashrate":1.02e+06,"every":300})"));
    shareBody(out, sizeof(out), 987654321.0); assert(strstr(out, "\"hashrate\":9.88e+08,"));
    shareBody(out, sizeof(out), 0); assert(strstr(out, "\"hashrate\":0,"));
    shareBody(out, sizeof(out), NAN); assert(strstr(out, "\"hashrate\":0,"));
    assert(shareBody(out, 20, 1e6) == 0);

    sharePoll(); assert(posted.empty());                       // not before 3 minutes
    nowMs = kFirstMs; sharePoll(); assert(posted.size() == 1);
    assert(hosts.back() == "stats.example");
    assert(posted[0] == R"({"v":"1.9.0","board":"m5stickc-plus2","hashrate":1.02e+06,"every":300})");
    assert(kEveryMs == kEverySeconds * 1000 && kEverySeconds == 300);
    nowMs += kEveryMs - 1; sharePoll(); assert(posted.size() == 1);
    nowMs += 1; answer = 500; sharePoll(); assert(posted.size() == 2);
    nowMs += 60000; sharePoll(); assert(posted.size() == 2);   // a failure waits for the next report
    nowMs = next; rate = 0; sharePoll(); assert(posted.size() == 2);  // paused: nothing sent
    nowMs += kWaitMs; rate = 1e6; memoryOk = false; sharePoll(); assert(posted.size() == 2);
    nowMs += kWaitMs; memoryOk = true; settings.shareHashrate = false; sharePoll(); assert(posted.size() == 2);
    settings.shareHashrate = true; sharePoll(); assert(posted.size() == 3);
    puts("share: OK");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'share_test.cpp'
    src.write_text(prefix + body + tests)
    exe = Path(tmp) / 'share_test'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wno-unused-function', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
