#include "notify.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/queue.h>

#include "log.h"
#include "settings.h"
#include "tls.h"

namespace {

struct Message {
    char title[64];
    char text[192];
    char tags[32];
    uint8_t priority;
};

QueueHandle_t queue;
constexpr uint32_t kMinHeap = 70 * 1024;  // a TLS session needs ~45 KB

bool send(const Message &m) {
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
    if (!http.begin(tls, "https://" + settings.ntfyServer + "/" + settings.ntfyTopic)) return false;
    if (!tlsConnect(tls, host.c_str(), port)) return false;
    http.addHeader("Title", m.title);
    http.addHeader("Priority", String(m.priority));
    if (m.tags[0]) http.addHeader("Tags", m.tags);
    int code = http.POST((uint8_t *)m.text, strlen(m.text));
    http.end();
    if (code != HTTP_CODE_OK) logf("[notify] HTTP %d\n", code);
    return code == HTTP_CODE_OK;
}

void notifyTask(void *) {
    Message m;
    for (;;) {
        xQueueReceive(queue, &m, portMAX_DELAY);
        for (int attempt = 0; attempt < 3; attempt++) {
            if (WiFi.isConnected() && ESP.getFreeHeap() > kMinHeap && send(m)) {
                logf("[notify] sent: %s\n", m.title);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }
}

}  // namespace

void notifyStart() {
    if (settings.ntfyTopic.isEmpty()) return;  // changes take effect after reboot
    queue = xQueueCreate(6, sizeof(Message));
    xTaskCreatePinnedToCore(notifyTask, "notify", 10240, nullptr, 1, nullptr, 0);
}

void notify(const char *title, const char *message, int priority, const char *tags) {
    if (!queue || settings.ntfyTopic.isEmpty()) return;
    Message m{};
    strlcpy(m.title, title, sizeof(m.title));
    strlcpy(m.text, message, sizeof(m.text));
    strlcpy(m.tags, tags, sizeof(m.tags));
    m.priority = priority;
    if (xQueueSend(queue, &m, 0) != pdTRUE) logf("[notify] queue full, dropped: %s\n", title);
}
