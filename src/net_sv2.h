#pragma once
#include <WiFi.h>
#include <atomic>
#include <string>

namespace sv2 {
struct SessionResult { bool authorized = false, timedOut = false; std::string refusal; };
SessionResult session(WiFiClient &client, uint32_t maxMs, const char *identity,
                      void (*status)(const char *), std::atomic<uint8_t> &pending);
}
