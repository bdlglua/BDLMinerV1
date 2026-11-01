#pragma once
#include <Arduino.h>
#include <cassert>
#include <deque>
#include <functional>
#include <vector>
#include "WiFiClientSecure.h"
struct Stream {
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() = 0;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *, size_t) = 0;
};
struct Reply { int code; std::string body; int length = -2; };
inline std::deque<Reply> replies;
inline std::vector<std::string> urls;
inline std::function<void()> bodyHook;
struct HTTPClient {
    std::string url;
    Reply reply{500, ""};
    void setReuse(bool) {}
    void setTimeout(unsigned) {}
    bool begin(WiFiClientSecure &, const String &u) { url = u; return true; }
    int GET() {
        assert(!replies.empty()); urls.push_back(url); reply = replies.front(); replies.pop_front(); return reply.code;
    }
    int getSize() { return reply.length == -2 ? int(reply.body.size()) : reply.length; }
    int writeToStream(Stream *out) {
        if (bodyHook) { auto hook = bodyHook; bodyHook = {}; hook(); }
        for (size_t at = 0; at < reply.body.size();) {
            size_t size = std::min<size_t>(7, reply.body.size() - at);
            if (out->write((const uint8_t *)reply.body.data() + at, size) != size) return -1;
            at += size;
        }
        return reply.body.size();
    }
    void end() {}
};
