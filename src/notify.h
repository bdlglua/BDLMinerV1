// Push notifications through ntfy (https://ntfy.sh or a self-hosted server):
// install the ntfy app, subscribe to the topic set in the settings. Each one
// is also posted as JSON to the webhook URL when one is set.
#pragma once

#include <Arduino.h>

void notifyStart();

// Queues a notification (dropped when no topic is set or the queue is full).
// priority: 1 (min) .. 5 (max), tags: ntfy emoji tags ("tada,moneybag").
void notify(const char *title, const char *message, int priority = 3, const char *tags = "");

// Sends a test notification; false when none is configured (since boot).
bool notifyTest();
// The last delivery: {"id":N,"title":...,"ntfy":"OK"|"HTTP 404"|"off"...,"webhook":...};
// id 0 before the first one.
String notifyResult();
