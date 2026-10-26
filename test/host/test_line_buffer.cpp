#include <cassert>
#include <cstdio>
#include "core/line_buffer.h"
#include "core/stratum.h"

int main() {
    using Result = stratum::LineBuffer::Result;
    stratum::LineBuffer input(64);
    // A valid-looking prefix followed by excess bytes must never be parsed
    // as a shorter, valid frame. Even embedded CR and NUL count toward size.
    std::string huge = "{\"id\":2,\"result\":true,\"error\":null}" + std::string(1000, ' ');
    for (char c : huge) assert(input.push(c) == Result::More);
    assert(input.push('\n') == Result::Oversized);
    assert(input.line().empty());
    for (char c : std::string("{\"id\":2,\"result\":true}")) input.push(c);
    input.push('\0');
    for (char c : std::string("hidden trailing bytes")) input.push(c);
    assert(input.push('\n') == Result::Oversized && input.line().empty());
    stratum::Protocol protocol;
    protocol.reset();
    assert(!protocol.authorized());
    for (char c : std::string("{\"id\":2,\"result\":true,\"error\":null}\r"))
        assert(input.push(c) == Result::More);
    assert(input.push('\n') == Result::Complete);
    assert(protocol.handleLine(input.line().c_str()) == stratum::Event::Authorized);
    assert(protocol.authorized());
    input.clear();
    for (int i = 0; i < 64; i++) assert(input.push('a') == Result::More);
    assert(input.push('\n') == Result::Complete && input.line().size() == 64);
    input.clear();
    for (int i = 0; i < 65; i++) input.push('\0');
    assert(input.push('\n') == Result::Oversized);
    puts("Stratum framing: bounded input, full-frame discard and recovery passed");
}
