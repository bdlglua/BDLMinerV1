#pragma once
inline bool testOnline = true;
struct MockWiFi { bool isConnected() { return testOnline; } };
inline MockWiFi WiFi;
