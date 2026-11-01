#include "notify.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/queue.h>

#include "log.h"
#include "settings.h"
#include "tls.h"
#include "runtime.h"
#include "web.h"

namespace {

struct Message {
    char title[64];
    char text[192];
    char tags[32];
    uint8_t priority;
    uint32_t testId;
};

QueueHandle_t queue;
portMUX_TYPE resultMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t resultId = 0;
char resultTitle[64] = "", resultNtfy[24] = "off", resultHook[24] = "off";
struct TestResult {
    uint32_t id = 0;
    char ntfy[24] = "off", webhook[24] = "off";
};
constexpr size_t kTestResults = 8;
TestResult testResults[kTestResults];
size_t testHead = 0;
uint32_t nextTestId = 0;  // guarded by resultMux

bool enabled() { return !settings.ntfyTopic.isEmpty() || !settings.webhookUrl.isEmpty(); }

bool enqueue(const Message &m) {
    if (!queue || !enabled()) return false;
    if (xQueueSend(queue, &m, 0) == pdTRUE) return true;
    logf("[notify] queue full, dropped: %s\n", m.title);
    return false;
}

// "https://host[:port]/path" -> host, port. False for anything else.
bool splitUrl(const String &url, String &host, uint16_t &port, bool &secure) {
    secure = url.startsWith("https://");
    if (!secure && !url.startsWith("http://")) return false;
    int start = secure ? 8 : 7, end = start;
    while (end < (int)url.length() && url[end] != '/' && url[end] != '?' && url[end] != '#') end++;
    host = url.substring(start, end);
    port = secure ? 443 : 80;
    int colon = host.lastIndexOf(':');
    if (colon > 0) {
        port = host.substring(colon + 1).toInt() ?: port;
        host.remove(colon);
    }
    return host.length() > 0;
}

// One JSON body for the common receivers: Discord reads "content", Telegram's
// sendMessage "text" (chat_id in the URL), anything else the other fields.
// HTTP status, or -1 when no connection was made.
int sendWebhook(const Message &m) {
    String host;
    uint16_t port;
    bool secure;
    if (!splitUrl(settings.webhookUrl, host, port, secure)) return -1;
    JsonDocument d;
    d["content"] = String("**") + m.title + "**\n" + m.text;
    d["text"] = String(m.title) + ": " + m.text;
    d["title"] = m.title;
    d["message"] = m.text;
    d["priority"] = m.priority;
    d["tags"] = m.tags;
    d["miner"] = webHostName();
    d["firmware"] = FW_VERSION;
    String body;
    serializeJson(d, body);
    WiFiClientSecure tls;
    WiFiClient tcp;
    HTTPClient http;
    http.setTimeout(10000);
    if (!(secure ? http.begin(tls, settings.webhookUrl) : http.begin(tcp, settings.webhookUrl))) return -1;
    if (secure && !tlsConnect(tls, host.c_str(), port)) return -1;
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    http.end();
    if (code < 200 || code >= 300) logf("[notify] webhook HTTP %d\n", code);
    return code;
}

int sendNtfy(const Message &m) {
    // "ntfy.sh", or a self-hosted "ntfy.example.com:8443"
    String host = settings.ntfyServer;
    uint16_t port = 443;
    int colon = host.lastIndexOf(':');
    if (colon > 0) {
        port = host.substring(colon + 1).toInt() ?: 443;
        host.remove(colon);
    }
    WiFiClientSecure tls;
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(tls, "https://" + settings.ntfyServer + "/" + settings.ntfyTopic)) return -1;
    if (!tlsConnect(tls, host.c_str(), port)) return -1;
    http.addHeader("Title", m.title);
    http.addHeader("Priority", String(m.priority));
    if (m.tags[0]) http.addHeader("Tags", m.tags);
    int code = http.POST((uint8_t *)m.text, strlen(m.text));
    http.end();
    if (code != HTTP_CODE_OK) logf("[notify] HTTP %d\n", code);
    return code;
}

bool ok(int code) { return code >= 200 && code < 300; }

void describe(char *out, size_t size, bool configured, int code) {
    if (!configured) strlcpy(out, "off", size);
    else if (ok(code)) strlcpy(out, "OK", size);
    else if (code > 0) snprintf(out, size, "HTTP %d", code);
    else if (code == 0) strlcpy(out, "no WiFi or memory", size);
    else strlcpy(out, "no connection", size);
}

// One message being delivered. Each destination is retried on its own: one
// failing must not repeat the other.
struct Delivery {
    Message m;
    bool active = false, useNtfy = false, useHook = false;
    uint8_t attempts = 0;
    int ntfy = 0, hook = 0;  // last HTTP status, -1 no connection, 0 not tried
    uint32_t nextAt = 0, waitingSince = 0;
} delivery;
constexpr uint8_t kAttempts = 3;
constexpr uint32_t kRetryMs = 30 * 1000;
constexpr uint32_t kMemoryWaitMs = 10 * 60 * 1000;  // low memory or no WiFi: wait, without using up attempts

void finish(Delivery &d) {
    bool sent = (!d.useNtfy || ok(d.ntfy)) && (!d.useHook || ok(d.hook));
    logf("[notify] %s: %s\n", sent ? "sent" : "not delivered", d.m.title);
    char n[24], h[24];
    describe(n, sizeof(n), d.useNtfy, d.ntfy);
    describe(h, sizeof(h), d.useHook, d.hook);
    portENTER_CRITICAL(&resultMux);
    resultId++;
    strlcpy(resultTitle, d.m.title, sizeof(resultTitle));
    strlcpy(resultNtfy, n, sizeof(resultNtfy));
    strlcpy(resultHook, h, sizeof(resultHook));
    if (d.m.testId) {
        TestResult &test = testResults[testHead];
        test.id = d.m.testId;
        memcpy(test.ntfy, n, sizeof(test.ntfy));
        memcpy(test.webhook, h, sizeof(test.webhook));
        testHead = (testHead + 1) % kTestResults;
    }
    portEXIT_CRITICAL(&resultMux);
    d.active = false;
}

}  // namespace

void notifyStart() {
    if (!enabled()) return;  // changes take effect after reboot
    queue = xQueueCreate(6, sizeof(Message));
    runtime::started(queue != nullptr, runtime::Notify, "notification queue");
}

void notifyPoll() {
    Delivery &d = delivery;
    if (!queue) return;
    uint32_t now = millis();
    if (!d.active) {
        if (xQueueReceive(queue, &d.m, 0) != pdTRUE) return;
        d.active = true;
        d.useNtfy = !settings.ntfyTopic.isEmpty();
        d.useHook = !settings.webhookUrl.isEmpty();
        d.attempts = 0;
        d.ntfy = d.hook = 0;
        d.nextAt = d.waitingSince = now;
    }
    if ((int32_t)(now - d.nextAt) < 0) return;
    if (!WiFi.isConnected() || !tlsMemoryOk()) {
        if (now - d.waitingSince < kMemoryWaitMs) d.nextAt = now + 10000;
        else finish(d);
        return;
    }
    d.attempts++;
    if (d.useNtfy && !ok(d.ntfy)) d.ntfy = sendNtfy(d.m);
    if (d.useHook && !ok(d.hook)) d.hook = sendWebhook(d.m);
    if (((!d.useNtfy || ok(d.ntfy)) && (!d.useHook || ok(d.hook))) || d.attempts >= kAttempts) {
        finish(d);
    } else {
        d.nextAt = now + kRetryMs;
        d.waitingSince = d.nextAt;
    }
}

void notify(const char *title, const char *message, int priority, const char *tags) {
    if (!queue || !enabled()) return;
    Message m{};
    strlcpy(m.title, title, sizeof(m.title));
    strlcpy(m.text, message, sizeof(m.text));
    strlcpy(m.tags, tags, sizeof(m.tags));
    m.priority = priority;
    enqueue(m);
}

uint32_t notifyTest() {
    if (!queue || !enabled()) return 0;
    Message m{};
    strlcpy(m.title, "Test notification", sizeof(m.title));
    snprintf(m.text, sizeof(m.text), "Test from %s (firmware %s): notifications work.", webHostName().c_str(), FW_VERSION);
    strlcpy(m.tags, "bell", sizeof(m.tags));
    m.priority = 3;
    portENTER_CRITICAL(&resultMux);
    if (!++nextTestId) ++nextTestId;  // zero means no result, including at wrap
    m.testId = nextTestId;
    portEXIT_CRITICAL(&resultMux);
    return enqueue(m) ? m.testId : 0;
}

String notifyResult(uint32_t id) {
    TestResult result;
    portENTER_CRITICAL(&resultMux);  // copies only: no allocation with interrupts off
    if (!id) result = testResults[(testHead + kTestResults - 1) % kTestResults];
    else for (const TestResult &test : testResults) if (test.id == id) { result = test; break; }
    portEXIT_CRITICAL(&resultMux);
    JsonDocument d;
    d["id"] = result.id;
    d["title"] = result.id ? "Test notification" : "";
    d["ntfy"] = result.ntfy;
    d["webhook"] = result.webhook;
    String out;
    serializeJson(d, out);
    return out;
}

NotifyStatus notifyStatus() {
    NotifyStatus s;
    s.enabled = queue != nullptr;
    portENTER_CRITICAL(&resultMux);
    s.id = resultId;
    memcpy(s.title, resultTitle, sizeof(s.title));
    memcpy(s.ntfy, resultNtfy, sizeof(s.ntfy));
    memcpy(s.webhook, resultHook, sizeof(s.webhook));
    portEXIT_CRITICAL(&resultMux);
    return s;
}
