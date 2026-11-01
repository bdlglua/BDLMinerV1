#!/usr/bin/env python3
"""Exercise production notification queue/delivery code without HTTP traffic."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/notify.cpp').read_text()
state = source[source.index('namespace {'):source.index('// "https://host')]
delivery = source[source.index('bool ok(int code)'):source.index('void notifyStart()')]
api = source[source.index('void notifyStart()'):]
web = (root / 'src/web.cpp').read_text()
handlers = web[web.index('void handleNotifyTest()'):web.index('// POST /update (multipart):')]
prefix = r'''
#include <ArduinoJson.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include "notify.h"
#include <freertos/queue.h>
#define FW_VERSION "test"
struct Text : std::string {
    using std::string::string;
    bool isEmpty() const { return empty(); }
};
struct { Text ntfyTopic = "topic", webhookUrl = "https://local.test/hook"; } settings;
struct { bool connected = true; bool isConnected() { return connected; } } WiFi;
bool memoryOk = true;
bool tlsMemoryOk() { return memoryOk; }
void logf(const char *, ...) {}
String webHostName() { return "test-miner"; }
namespace runtime { enum { Notify }; void started(bool, int, const char *) {} }
int ntfyCalls = 0, hookCalls = 0, ntfyCode = 200, hookCode = 200;
struct {
    int status = 0;
    String query, body;
    String arg(const char *) { return query; }
    void send(int code, const char *, const String &text) { status = code; body = text; }
    void sendHeader(const char *, const char *) {}
} server;
bool authenticated = true;
bool authorized() { if (!authenticated) server.status = 401; return authenticated; }
'''
send = r'''
int sendNtfy(const Message &) { ++ntfyCalls; return ntfyCode; }
int sendWebhook(const Message &) { ++hookCalls; return hookCode; }
'''
tests = r'''
int main() {
    notifyStart();
    authenticated = false; handleNotifyTest();
    assert(server.status == 401 && queue->entries.empty()); authenticated = true;
    // A full queue must not acknowledge a test that it discarded.
    for (int i = 0; i < 6; ++i) notify("Alert", "queued");
    assert(!notifyTest() && queue->entries.size() == 6);
    handleNotifyTest(); assert(server.status == 503 && queue->entries.size() == 6);
    for (int i = 0; i < 6; ++i) notifyPoll();
    assert(ntfyCalls == 6 && hookCalls == 6);
    auto id = notifyTest(); assert(id);
    notifyPoll();
    JsonDocument result; assert(!deserializeJson(result, notifyResult()));
    assert(result["id"] == id && result["ntfy"] == "OK" && result["webhook"] == "OK");
    // An ordinary alert completing after the test must not replace its result.
    notify("Miner too hot", "alert"); notifyPoll();
    result.clear(); assert(!deserializeJson(result, notifyResult()));
    assert(result["id"] == id && result["title"] == "Test notification");
    assert(std::string(notifyStatus().title) == "Miner too hot");
    // Retry only the failed destination, then retain the final result.
    id = notifyTest(); assert(id); hookCode = 503;
    int beforeNtfy = ntfyCalls, beforeHook = hookCalls;
    notifyPoll(); assert(ntfyCalls == beforeNtfy + 1 && hookCalls == beforeHook + 1);
    testMillis += 30000; hookCode = 200; notifyPoll();
    assert(ntfyCalls == beforeNtfy + 1 && hookCalls == beforeHook + 2);
    result.clear(); assert(!deserializeJson(result, notifyResult()));
    assert(result["id"] == id && result["webhook"] == "OK");
    // Distinct tests can complete before either client polls its result.
    uint32_t first = notifyTest(), second = notifyTest(); assert(first && second != first);
    notifyPoll(); notifyPoll();
    for (uint32_t expected : {first, second}) {
        server.query = std::to_string(expected); handleNotifyResult();
        result.clear(); assert(server.status == 200 && !deserializeJson(result, server.body));
        assert(result["id"] == expected && result["ntfy"] == "OK");
    }
    // IDs remain unambiguous across uint32 wrap and HTTP parsing.
    nextTestId = UINT32_MAX - 1; handleNotifyTest();
    result.clear(); assert(server.status == 202 && !deserializeJson(result, server.body));
    assert(result["id"] == UINT32_MAX); notifyPoll();
    id = notifyTest(); assert(id == 1); notifyPoll();
    server.query = "4294967295"; handleNotifyResult(); result.clear();
    assert(server.status == 200 && !deserializeJson(result, server.body) && result["id"] == UINT32_MAX);
    for (const char *bad : {"oops", "12junk", "-1", "4294967296", "9999999999999999999999999999"}) {
        server.query = bad; handleNotifyResult(); assert(server.status == 400);
    }
    // Retention is bounded; old results report pending/unknown instead of
    // returning another test's delivery status.
    for (unsigned i = 0; i < kTestResults; ++i) { assert(notifyTest()); notifyPoll(); }
    result.clear(); assert(!deserializeJson(result, notifyResult(UINT32_MAX)) && result["id"] == 0);
    memoryOk = false; id = notifyTest(); int before = ntfyCalls; notifyPoll();
    assert(ntfyCalls == before); testMillis += 600000; notifyPoll();
    result.clear(); assert(!deserializeJson(result, notifyResult(id)) && result["id"] == id);
    assert(result["ntfy"] == "no WiFi or memory");
    delete queue; queue = nullptr; handleNotifyTest(); assert(server.status == 409);
    puts("Notifications: full queue refused, test/result correlation, later alerts isolated and per-destination retries passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-notify-') as temp:
    cpp = Path(temp) / 'test.cpp'
    exe = Path(temp) / 'test'
    cpp.write_text(prefix + state + send + delivery + api + handlers + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Itest/host/stubs/miner', '-Isrc',
                    '-I.pio/libdeps/esp32-headless/ArduinoJson/src', str(cpp), '-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], check=True)
