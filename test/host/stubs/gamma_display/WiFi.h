#pragma once
#include <Arduino.h>
inline String testIP = "192.168.1.42";
struct PreviewIP { String toString() const { return testIP; } };
struct PreviewWiFi { PreviewIP localIP() const { return {}; } };
inline PreviewWiFi WiFi;
