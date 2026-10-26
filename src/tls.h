// TLS connections made safe for the miner: one handshake at a time, with the
// hardware SHA loop paused and core 0's idle watchdog off meanwhile.
#pragma once

#include <WiFiClientSecure.h>

// Connects `tls` (certificate checks off: only public data is fetched, or
// notifications sent). Then use it with HTTPClient, which reuses the connection.
bool tlsConnect(WiFiClientSecure &tls, const char *host, uint16_t port = 443);

// Turn core 0's idle-task watchdog off and back on for a handshake.
void core0WdtPause();
void core0WdtResume();
struct TlsProfile {
    uint32_t handshakes = 0, failures = 0, lastMs = 0;
    uint64_t handshakeMs = 0;
};
TlsProfile tlsProfile();
