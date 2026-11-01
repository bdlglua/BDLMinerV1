// Push notifications through ntfy (https://ntfy.sh or a self-hosted server):
// install the ntfy app, subscribe to the topic set in the settings. Each one
// is also posted as JSON to the webhook URL when one is set.
#pragma once

#include <Arduino.h>

void notifyStart();
// Delivers queued notifications, one step at a time; called every few seconds
// from the market task, which already makes TLS connections.
void notifyPoll();

// Queues a notification (dropped when no topic is set or the queue is full).
// priority: 1 (min) .. 5 (max), tags: ntfy emoji tags ("tada,moneybag").
void notify(const char *title, const char *message, int priority = 3, const char *tags = "");

// Queues a test; returns its ID, or 0 when unavailable/full.
uint32_t notifyTest();
// A completed test (0 = latest), retained among the last eight test results.
// {"id":N,"title":...,"ntfy":"OK"|"HTTP 404"|"off"...,"webhook":...};
// id 0 means this test has no retained result yet. Ordinary alerts do not replace it.
String notifyResult(uint32_t id = 0);
// For the dashboard: whether notifications are on and the last delivery.
struct NotifyStatus {
    bool enabled = false;
    uint32_t id = 0;  // 0 before the first delivery
    char title[64] = "", ntfy[24] = "off", webhook[24] = "off";
};
NotifyStatus notifyStatus();
