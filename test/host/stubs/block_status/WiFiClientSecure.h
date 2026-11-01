#pragma once
struct WiFiClientSecure {
    bool open = false;
    bool connected() const { return open; }
    void stop() { open = false; }
};
