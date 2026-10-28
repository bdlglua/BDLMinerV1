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
};

QueueHandle_t queue;
portMUX_TYPE resultMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t resultId = 0;
char resultTitle[64] = "", resultNtfy[24] = "off", resultHook[24] = "off";
constexpr uint32_t kMinHeap = 70 * 1024;  // a TLS session needs ~45 KB

bool enabled() { return !settings.ntfyTopic.isEmpty() || !settings.webhookUrl.isEmpty(); }

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

void notifyTask(void *) {
    Message m;
    for (;;) {
        xQueueReceive(queue, &m, portMAX_DELAY);
        // Each destination is retried on its own: one failing must not repeat the other.
        bool useNtfy = !settings.ntfyTopic.isEmpty(), useHook = !settings.webhookUrl.isEmpty();
        int ntfy = 0, hook = 0;  // last HTTP status, -1 no connection, 0 not tried
        for (int attempt = 0; attempt < 3 && ((useNtfy && !ok(ntfy)) || (useHook && !ok(hook))); attempt++) {
            if (attempt) vTaskDelay(pdMS_TO_TICKS(30000));
            if (!WiFi.isConnected() || ESP.getFreeHeap() <= kMinHeap) continue;
            if (useNtfy && !ok(ntfy)) ntfy = sendNtfy(m);
            if (useHook && !ok(hook)) hook = sendWebhook(m);
        }
        bool sent = (!useNtfy || ok(ntfy)) && (!useHook || ok(hook));
        logf("[notify] %s: %s\n", sent ? "sent" : "not delivered", m.title);
        char n[24], h[24];
        describe(n, sizeof(n), useNtfy, ntfy);
        describe(h, sizeof(h), useHook, hook);
        portENTER_CRITICAL(&resultMux);
        resultId++;
        strlcpy(resultTitle, m.title, sizeof(resultTitle));
        strlcpy(resultNtfy, n, sizeof(resultNtfy));
        strlcpy(resultHook, h, sizeof(resultHook));
        portEXIT_CRITICAL(&resultMux);
    }
}

}  // namespace

void notifyStart() {
    if (!enabled()) return;  // changes take effect after reboot
    queue = xQueueCreate(6, sizeof(Message));
    if (!runtime::started(queue != nullptr, runtime::Notify, "notification queue")) return;
    if (!runtime::started(xTaskCreatePinnedToCore(notifyTask, "notify", 10240, nullptr, 1, nullptr, 0) == pdPASS,
                          runtime::Notify, "notifications")) {
        vQueueDelete(queue);
        queue = nullptr;
    }
}

void notify(const char *title, const char *message, int priority, const char *tags) {
    if (!queue || !enabled()) return;
    Message m{};
    strlcpy(m.title, title, sizeof(m.title));
    strlcpy(m.text, message, sizeof(m.text));
    strlcpy(m.tags, tags, sizeof(m.tags));
    m.priority = priority;
    if (xQueueSend(queue, &m, 0) != pdTRUE) logf("[notify] queue full, dropped: %s\n", title);
}

bool notifyTest() {
    if (!queue || !enabled()) return false;
    char text[96];
    snprintf(text, sizeof(text), "Test from %s (firmware %s): notifications work.", webHostName().c_str(), FW_VERSION);
    notify("Test notification", text, 3, "bell");
    return true;
}

String notifyResult() {
    char title[64], ntfy[24], hook[24];
    portENTER_CRITICAL(&resultMux);  // copies only: no allocation with interrupts off
    uint32_t id = resultId;
    memcpy(title, resultTitle, sizeof(title));
    memcpy(ntfy, resultNtfy, sizeof(ntfy));
    memcpy(hook, resultHook, sizeof(hook));
    portEXIT_CRITICAL(&resultMux);
    JsonDocument d;
    d["id"] = id;
    d["title"] = title;
    d["ntfy"] = ntfy;
    d["webhook"] = hook;
    String out;
    serializeJson(d, out);
    return out;
}
