#pragma once
#include <Arduino.h>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
inline void (*readHook)() = nullptr;
struct WiFiClient {
    int fd;
    explicit WiFiClient(int value) : fd(value) {}
    bool connected() {
        char byte; int n = recv(fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
        return n > 0 || (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    }
    int read(uint8_t *out, size_t length) {
        ++testMillis;
        if (readHook) readHook(); // deliver a simulated UART result between network polls
        int n = recv(fd, out, std::min<size_t>(length, 7), MSG_DONTWAIT);
        if (n < 0) { usleep(1000); return 0; }
        return n;
    }
    size_t write(const uint8_t *data, size_t length) {
        int n = send(fd, data, std::min<size_t>(length, 3), MSG_NOSIGNAL);
        return n > 0 ? size_t(n) : 0;
    }
};
struct LocalWiFi { bool isConnected() { return true; } };
inline LocalWiFi WiFi;
