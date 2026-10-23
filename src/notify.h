// Push notifications through ntfy (https://ntfy.sh or a self-hosted server):
// install the ntfy app, subscribe to the topic set in the settings.
#pragma once

void notifyStart();

// Queues a notification (dropped when no topic is set or the queue is full).
// priority: 1 (min) .. 5 (max), tags: ntfy emoji tags ("tada,moneybag").
void notify(const char *title, const char *message, int priority = 3, const char *tags = "");
