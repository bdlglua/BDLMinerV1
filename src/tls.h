// TLS connections made safe for the miner: one handshake at a time, with the
// hardware SHA loop paused and core 0's idle watchdog off meanwhile.
#pragma once

#include <WiFiClientSecure.h>

// Connects `tls`, verifying the server's certificate chain and name against
// the roots of src/tls_roots.h (assets/tls/roots.pem), then use it with
// HTTPClient, which reuses the connection. Fails while the clock is not set
// and never falls back to an unverified connection: notification topics and
// webhook URLs hold secrets. A short `timeoutSeconds` suits probes of
// servers that may not speak TLS.
bool tlsConnect(WiFiClientSecure &tls, const char *host, uint16_t port = 443, uint8_t timeoutSeconds = 12);
// For downloads whose content carries its own signature (firmware releases)
// only: verified like tlsConnect(), but a certificate it cannot verify (a root
// missing from the bundle, an unset clock) falls back to an unverified
// connection rather than stopping updates. Network failures are not retried.
bool tlsConnectSignedContent(WiFiClientSecure &tls, const char *host, uint16_t port = 443, uint8_t timeoutSeconds = 12);
// The last failed handshake was refused for its certificate (or the clock).
bool tlsCertificateRejected();
// Why the last handshake failed, as mbedTLS says it ("" after a success).
const char *tlsLastError();
// Its mbedTLS code (0 after a success). kTlsPeerAlert: the server refused the
// handshake (up to 2.4.2, typically a server that only accepted TLS 1.3).
int tlsLastErrorCode();
constexpr int kTlsPeerAlert = -0x7780;
bool tlsClockSet();
// Same miner/watchdog/heap safeguards, with CA and hostname verification.
// Missing trust data or a certificate failure never falls back to insecure TLS.
bool tlsConnectVerified(WiFiClientSecure &tls, const char *host, const char *rootCA,
                        uint16_t port = 443, uint8_t timeoutSeconds = 12);

// Reads a response body over TLS without stopping between records.
// Arduino-ESP32 3.x's NetworkClient::readBytes() gives up at the first read()
// that finds no data, which on TLS is any moment the next record has not
// arrived yet: a body spread over several records (mempool.space's block
// list, GitHub's release list) came out truncated. This waits up to
// `timeoutMs` without data, and stops early once the connection is closed.
// Usable as an ArduinoJson reader (read() and readBytes()).
class TlsBodyReader {
public:
    TlsBodyReader(WiFiClientSecure &client, uint32_t timeoutMs = 10000) : client(client), timeoutMs(timeoutMs) {}
    int read();
    size_t readBytes(char *buffer, size_t length);
    size_t readBytes(uint8_t *buffer, size_t length) { return readBytes((char *)buffer, length); }

private:
    WiFiClientSecure &client;
    uint32_t timeoutMs;
};

// Enough free memory for one session: two 16 KB record buffers and the
// handshake, with some left for the other tasks. Below it, wait.
constexpr uint32_t kTlsMinFree = 54 * 1024, kTlsMinBlock = 24 * 1024;
bool tlsMemoryOk();

// Turn core 0's idle-task watchdog off and back on for a handshake.
void core0WdtPause();
void core0WdtResume();
struct TlsProfile {
    uint32_t handshakes = 0, failures = 0, lastMs = 0;
    uint64_t handshakeMs = 0;
};
TlsProfile tlsProfile();
