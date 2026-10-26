// Bound network input and discard a whole oversized frame before resuming.
#pragma once
#include <cstddef>
#include <string>

namespace stratum {
class LineBuffer {
    std::string buffer;
    size_t limit;
    bool dropping = false;
public:
    enum class Result { More, Complete, Oversized };
    explicit LineBuffer(size_t maximum) : limit(maximum) { buffer.reserve(512); }
    Result push(char c) {
        if (c == '\n') {
            if (dropping) { dropping = false; buffer.clear(); return Result::Oversized; }
            return Result::Complete;
        }
        if (!dropping) {
            if (c == '\0' || buffer.size() >= limit) { dropping = true; buffer.clear(); }
            else buffer += c;
        }
        return Result::More;
    }
    const std::string &line() const { return buffer; }
    void clear() { buffer.clear(); }
};
}  // namespace stratum
