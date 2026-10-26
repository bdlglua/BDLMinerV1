#!/usr/bin/env python3
"""Exercise the production Stratum session with deterministic pool replies."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/net.cpp').read_text()
send = source[source.index('bool sendLine('):source.index('bool sessionTimedOut')]
session = source[source.index('bool sessionTimedOut'):source.index('void netTask(')]
prefix = r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "core/stratum.h"
#include "core/line_buffer.h"
#include "core/pool_agent.h"
#define FW_VERSION "test"
#define DEFAULT_USER_AGENT "BDLMinerV1/" FW_VERSION
#define pdMS_TO_TICKS(x) (x)
using String = std::string;
struct { double suggestedDifficulty = 0.001; bool userAgentAuto = true; String userAgent = DEFAULT_USER_AGENT; } settings;
struct { bool isConnected() { return true; } } WiFi;
struct WiFiClient {
    std::string input;
    std::vector<std::string> sent;
    size_t maxWrite = SIZE_MAX, readFrames = SIZE_MAX;
    bool failWrite = false;
    bool closed = false, yieldRead = false;
    explicit WiFiClient(const char *frames) : input(frames) {}
    bool connected() { return !closed; }
    int read(uint8_t *out, size_t size) {
        if (yieldRead) { yieldRead = false; return 0; }
        if (input.empty()) { closed = true; return 0; }
        size_t n = std::min(size, input.size());
        if (readFrames == 1) { n = std::min(n, input.find('\n') + 1); yieldRead = true; }
        std::memcpy(out, input.data(), n); input.erase(0, n); return n;
    }
    size_t write(const uint8_t *line, size_t size) {
        if (failWrite) return 0;
        size = std::min(size, maxWrite); sent.emplace_back((const char *)line, size); return size;
    }
};
constexpr size_t kMaxLine = 16384;
constexpr uint32_t kHandshakeTimeoutMs = 30000, kPoolSilenceTimeoutMs = 300000, kShareAckTimeoutMs = 60000;
uint32_t millis() { return 1000; }
void vTaskDelay(uint32_t) {}
void logf(const char *, ...) {}
std::string status;
void setStatus(const char *s) { status = s; }
std::atomic<bool> onBackup{false};
std::atomic<uint8_t> pendingShares{0};
String host;
const String &netPoolHost() { return host; }
uint16_t netPoolPort() { return 3333; }
bool poolConnected = false;
int published = 0, premature = 0, polled = 0;
void minerPoolConnected(bool v) { poolConnected = v; }
void minerPublishJob(const work::Job &, bool) { ++published; if (!poolConnected) ++premature; }
void minerPoolDifficulty(double) {}
void minerShareResult(bool) {}
bool minerNextShare(stratum::Share &, uint32_t) { ++polled; if (!poolConnected) ++premature; return false; }
'''
tests = r'''
int main() {
    WiFiClient shortWrite("alive"); shortWrite.maxWrite = 3;
    assert(sendLine(shortWrite, "0123456789\n"));
    std::string complete; for (const auto &part : shortWrite.sent) complete += part;
    assert(complete == "0123456789\n");
    shortWrite.failWrite = true; assert(!sendLine(shortWrite, "x"));
    const char *ok = "{\"id\":1,\"result\":[[],\"aabb\",4],\"error\":null}\n"
                     "{\"id\":2,\"result\":true,\"error\":null}\n";
    const char *refusal = "{\"id\":1,\"result\":null,\"error\":[20,\"Only allowed user agents may subscribe\",null]}\n";
    stratum::Protocol protocol;
    protocol.setCredentials("address.worker", "x");
    stratum::PoolAgent main, backup;
    host = "pool.nerdminers.org";
    WiFiClient known(ok);
    session(known, protocol, 0, main);
    assert(known.sent.size() == 3 && known.sent[0].find(DEFAULT_USER_AGENT) != std::string::npos);
    assert(poolConnected && status == "Mining" && !retryAgent);

    host = "custom.example"; protocol.reset(); poolConnected = false;
    WiFiClient refused(refusal);
    session(refused, protocol, 0, main);
    assert(refused.sent.size() == 1 && refused.sent[0].find("BDLMinerV1/test") != std::string::npos);
    assert(retryAgent && !poolConnected);
    retryAgent = false; protocol.reset();
    WiFiClient retried(ok);
    session(retried, protocol, 0, main);
    assert(retried.sent[0].find("NerdMinerV2") != std::string::npos && poolConnected);
    assert(!strcmp(activeAgent.load(), "NerdMinerV2"));
    protocol.reset();
    WiFiClient refusedAgain(refusal);
    session(refusedAgain, protocol, 0, main);
    assert(!retryAgent); // already compatible: do not loop

    host = "backup.example"; onBackup = true; protocol.reset();
    WiFiClient secondary(ok);
    session(secondary, protocol, 0, backup);
    assert(secondary.sent[0].find("BDLMinerV1/test") != std::string::npos);
    assert(status == "Mining (backup pool)");

    host = "auth.example"; protocol.reset(); poolConnected = false;
    WiFiClient auth("{\"id\":1,\"result\":[[],\"aabb\",4],\"error\":null}\n"
                    "{\"id\":2,\"result\":false,\"error\":[24,\"Invalid worker\",null]}\n");
    session(auth, protocol, 0, backup);
    assert(!retryAgent && !poolConnected && status == "Auth failed: check address");
    host = "pool.nerdminers.org"; settings.userAgentAuto = false; settings.userAgent = "MyManualMiner";
    protocol.reset(); onBackup = false;
    WiFiClient manual(refusal);
    session(manual, protocol, 0, main);
    assert(manual.sent[0].find("MyManualMiner") != std::string::npos && !retryAgent);

    // A pool may notify between subscribe and authorize. Keep its job, but
    // neither publish it nor poll shares until the worker is authorized.
    protocol.reset(); poolConnected = false; published = premature = polled = 0;
    WiFiClient early("{\"id\":1,\"result\":[[],\"aabb\",4],\"error\":null}\n"
        "{\"method\":\"mining.notify\",\"params\":[\"early\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0102\",\"03\",[],\"20000000\",\"1d00ffff\",\"6b49d200\",true]}\n"
        "{\"id\":2,\"result\":true,\"error\":null}\n");
    std::string deniedFrames = early.input;
    deniedFrames.replace(deniedFrames.rfind("true"), 4, "false");
    early.readFrames = 1; session(early, protocol, 0, main);
    assert(protocol.hasJob() && published == 1 && premature == 0 && polled >= 1);
    protocol.reset(); poolConnected = false; published = premature = polled = 0;
    WiFiClient denied(deniedFrames.c_str()); denied.readFrames = 1; session(denied, protocol, 0, main);
    assert(!poolConnected && published == 0 && premature == 0 && polled == 0);
    puts("Production Stratum session: partial writes, deferred early jobs, refused authorization, automatic/manual identity, retry bounds and backup isolation passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-session-') as temp:
    cpp = Path(temp) / 'test.cpp'
    exe = Path(temp) / 'test'
    cpp.write_text(prefix + send + session + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Isrc',
                    '-I.pio/libdeps/esp32-headless/ArduinoJson/src', str(cpp),
                    'src/core/stratum.cpp', 'src/core/work.cpp', 'src/core/sha256.cpp',
                    '-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], check=True)
