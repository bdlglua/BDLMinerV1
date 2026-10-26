// Bounded, transport-independent Stratum V2 extended-channel mining protocol.
#pragma once
#include "stratum.h"
#include <array>

namespace sv2 {
constexpr size_t kMaxPayload = 16384;
struct Frame {
    uint16_t extension; uint8_t type; std::vector<uint8_t> payload;
    Frame(uint16_t ext = 0, uint8_t message = 0, std::vector<uint8_t> data = {})
        : extension(ext), type(message), payload(std::move(data)) {}
};
enum class Event { None, Setup, Opened, Job, Target, Accepted, Rejected, Error };

class Protocol {
public:
    Frame setup(const char *host, uint16_t port, const char *agent, const char *firmware);
    Frame open(const char *user, float hashrate);
    Frame submit(const stratum::Share &share);
    Event handle(const Frame &frame);
    const work::Job &job() const { return current_; }
    bool hasJob() const { return haveCurrent_; }
    bool opened() const { return state_ == 2; }
    size_t pending() const { return pendingCount_; }
    uint32_t acknowledged() const { return acknowledged_; }
    double difficulty() const;
    const std::string &error() const { return error_; }
private:
    struct Slot { uint32_t id = 0; bool valid = false, future = true; work::Job job; };
    std::array<Slot, 8> jobs_;
    size_t nextSlot_ = 0, pendingCount_ = 0;
    uint32_t pending_[32]{}, sequence_ = 0, channel_ = 0, group_ = 0, acknowledged_ = 0;
    uint8_t target_[32]{}, previous_[32]{};
    uint32_t ntime_ = 0, nbits_ = 0;
    std::vector<uint8_t> prefix_;
    uint16_t extranonceSize_ = 0;
    bool havePrevious_ = false, haveCurrent_ = false;
    uint8_t state_ = 0;
    work::Job current_;
    std::string error_;
    Event fail(const char *message);
    void activate(Slot &slot, uint32_t ntime, bool clean);
    size_t pendingIndex(uint32_t sequence) const;
};
} // namespace sv2
