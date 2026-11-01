#!/usr/bin/env python3
"""Exercise the production Stratum session with deterministic pool replies."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/net.cpp').read_text()
send = source[source.index('bool sendLine('):source.index('bool sessionTimedOut')]
session = source[source.index('bool sessionTimedOut'):source.index('void netTask(')]
network_task = source[source.index('void netTask('):source.index('}  // namespace')]
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
using std::min;
#define FW_VERSION "test"
#define DEFAULT_USER_AGENT "BDLMinerV1/" FW_VERSION
#define pdMS_TO_TICKS(x) (x)
struct String : std::string {
    using std::string::string;
    String(const std::string &s) : std::string(s) {}
    bool isEmpty() const { return empty(); }
    bool startsWith(const char *s) const { return find(s) == 0; }
};
struct {
    double suggestedDifficulty = 0.001;
    bool userAgentAuto = true;
    String userAgent = DEFAULT_USER_AGENT, poolHost = "main.pool", backupHost = "backup.pool";
    String btcAddress = "address.worker", poolPassword = "x";
    uint16_t backupPort = 3333;
    uint8_t rotateHours = 0, poolProtocol = 0, backupProtocol = 0;
} settings;
struct { bool isConnected() { return true; } void reconnect() {} } WiFi;
struct EndNetworkTest {};
std::vector<std::string> attemptedHosts;
std::string scriptedFrames;
bool scriptedWork = false, runningNetworkTask = false;
struct WiFiClient {
    std::string input;
    std::vector<std::string> sent;
    size_t maxWrite = SIZE_MAX, readFrames = SIZE_MAX;
    bool failWrite = false;
    bool closed = false, yieldRead = false;
    bool stayOpen = false;
    WiFiClient() = default;
    explicit WiFiClient(const char *frames) : input(frames) {}
    bool connect(const char *host, uint16_t, uint32_t) {
        attemptedHosts.emplace_back(host);
        if (attemptedHosts.size() == 4) throw EndNetworkTest{};
        input = scriptedFrames; closed = false; stayOpen = !scriptedWork;
        return true;
    }
    void setNoDelay(bool) {}
    void stop() { closed = true; }
    bool connected() { return !closed; }
    int read(uint8_t *out, size_t size) {
        if (yieldRead) { yieldRead = false; return 0; }
        if (input.empty()) { closed = !stayOpen; return 0; }
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
constexpr int kFailoverAfter = 3;
constexpr uint32_t kBackupSessionMs = 1800000;
uint32_t clockMs = 1000;
uint32_t millis() { return clockMs; }
void vTaskDelay(uint32_t ms) { clockMs += ms; }
void logf(const char *, ...) {}
std::string status;
void setStatus(const char *s) { status = s; }
std::atomic<bool> onBackup{false};
std::atomic<uint8_t> pendingShares{0};
std::atomic<uint32_t> failovers{0};
String netStatus() { return status; }
bool wifiTryFallbacks() { return true; }
void minerInvalidate() {}
String host;
const String &netPoolHost() { return runningNetworkTask ? (onBackup ? settings.backupHost : settings.poolHost) : host; }
uint16_t netPoolPort() { return 3333; }
bool poolConnected = false;
int published = 0, premature = 0, polled = 0;
void minerPoolConnected(bool v) { poolConnected = v; }
void minerPublishJob(const work::Job &, bool) { ++published; if (!poolConnected) ++premature; }
double poolDifficulty = 0;
void minerPoolDifficulty(double value) { poolDifficulty = value; }
void minerShareResult(bool, const stratum::ShareAck &) {}
void minerShareSent(double, bool, uint32_t) {}
bool minerNextShare(stratum::Share &, uint32_t ms) { ++polled; if (!poolConnected) ++premature; clockMs += ms; return false; }
namespace sv2 {
struct SessionResult { bool authorized = false, timedOut = false, hasWork = false; std::string refusal; };
SessionResult session(WiFiClient &, uint32_t, const char *, void (*)(const char *), std::atomic<uint8_t> &) {
    return {true, false, scriptedWork, {}};
}
}
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
    protocol.reset(); poolConnected = false; poolDifficulty = 0;
    std::string changingFrames=deniedFrames;
    changingFrames.replace(changingFrames.rfind("false"),5,"true");
    changingFrames+="{\"method\":\"mining.set_difficulty\",\"params\":[2]}\n";
    WiFiClient changing(changingFrames.c_str());session(changing,protocol,0,main);
    assert(protocol.hasJob() && protocol.difficulty()==2 && protocol.job().difficulty==1);
    assert(poolDifficulty==protocol.job().difficulty); // active job, not the next-job difficulty
    // Authorization without usable work must not leave a miner stuck at zero
    // hashrate. The simulated socket stays connected but sends no initial job.
    protocol.reset(); poolConnected = false; clockMs = 1000;
    WiFiClient noWork(ok); noWork.stayOpen = true;
    session(noWork, protocol, 0, main);
    assert(protocol.authorized() && !protocol.hasJob());
    assert(clockMs <= 1000 + kHandshakeTimeoutMs + 20);
    assert(status == "Pool work timed out");
    protocol.reset(); clockMs = UINT32_MAX - 1000;
    uint32_t wrapStart = clockMs;
    WiFiClient wrapped(ok); wrapped.stayOpen = true;
    session(wrapped, protocol, 0, main);
    assert(clockMs - wrapStart <= kHandshakeTimeoutMs + 20 && status == "Pool work timed out");
    // Exercise the real reconnect/failover loop too: authorization alone is
    // not a successful mining session, on either protocol.
    runningNetworkTask = true;
    for (unsigned version : {0u, 1u}) {
        settings.poolProtocol = version;
        scriptedFrames = ok; scriptedWork = false;
        attemptedHosts.clear(); onBackup = false;
        try { netTask(nullptr); } catch (const EndNetworkTest &) {}
        assert((attemptedHosts == std::vector<std::string>{"main.pool", "main.pool", "main.pool", "backup.pool"}));
    }
    // Pools that do send work retain the original successful-session policy.
    settings.poolProtocol = 0; scriptedWork = true;
    scriptedFrames = ok;
    scriptedFrames += "{\"method\":\"mining.notify\",\"params\":[\"work\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0102\",\"03\",[],\"20000000\",\"1d00ffff\",\"6b49d200\",true]}\n";
    protocol.reset(); clockMs = 1000; sessionTimedOut = false;
    WiFiClient mining(scriptedFrames.c_str()); mining.stayOpen = true;
    session(mining, protocol, 40000, main);
    assert(protocol.hasJob() && sessionTimedOut && clockMs > 41000);
    assert(status == "Switching pool"); // valid work needs no refresh within 30 s
    attemptedHosts.clear(); onBackup = false;
    try { netTask(nullptr); } catch (const EndNetworkTest &) {}
    assert((attemptedHosts == std::vector<std::string>{"main.pool", "main.pool", "main.pool", "main.pool"}));
    puts("Production Stratum session: partial writes, deferred early jobs, refused authorization, automatic/manual identity, no-work timeout, V1/V2 failover and backup isolation passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-session-') as temp:
    cpp = Path(temp) / 'test.cpp'
    exe = Path(temp) / 'test'
    cpp.write_text(prefix + send + session + network_task + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Isrc',
                    '-I.pio/libdeps/esp32-headless/ArduinoJson/src', str(cpp),
                    'src/core/stratum.cpp', 'src/core/work.cpp', 'src/core/sha256.cpp',
                    '-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], check=True)
