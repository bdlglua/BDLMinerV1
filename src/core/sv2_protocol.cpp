#include "sv2_protocol.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sv2 {
namespace {
struct Reader {
    const std::vector<uint8_t> &b; size_t p = 0; bool ok = true;
    explicit Reader(const std::vector<uint8_t> &data) : b(data) {}
    uint32_t number(size_t n) {
        if (p + n > b.size()) { ok = false; return 0; }
        uint32_t value = 0; for (size_t i = 0; i < n; ++i) value |= uint32_t(b[p++]) << (8 * i);
        return value;
    }
    void bytes(uint8_t *out, size_t n) {
        if (p + n > b.size()) { ok = false; return; }
        if (n) memcpy(out, b.data() + p, n);
        p += n;
    }
    std::vector<uint8_t> vector(size_t n) {
        if (p + n > b.size()) { ok = false; return {}; }
        std::vector<uint8_t> v(b.begin() + p, b.begin() + p + n); p += n; return v;
    }
    std::string text() { auto v = vector(number(1)); return std::string(v.begin(), v.end()); }
    bool end() const { return ok && p == b.size(); }
};
void put(std::vector<uint8_t> &b, uint32_t value, size_t n = 4) {
    for (size_t i = 0; i < n; ++i) b.push_back(value >> (8 * i));
}
bool text(std::vector<uint8_t> &b, const char *s) {
    if (!s || strlen(s) > 255) return false;
    size_t n = strlen(s); b.push_back(n); b.insert(b.end(), s, s + n); return true;
}
bool zero(const uint8_t *p, size_t n) { uint8_t x = 0; while (n--) x |= *p++; return x == 0; }
}

Frame Protocol::setup(const char *host, uint16_t port, const char *agent, const char *firmware) {
    Frame f; f.type = 0;
    auto &b = f.payload; put(b, 0, 1); put(b, 2, 2); put(b, 2, 2); put(b, 0); // mining, version 2, extended jobs
    if (!text(b, host)) return {};
    put(b, port, 2);
    const char *hardware =
#if defined(BOARD_GAMMA601)
        "Bitaxe Gamma 601 / BM1370";
#elif defined(BOARD_NERDNOS)
        "NerdNOS / BM1397";
#else
        "ESP32";
#endif
    if (!text(b, agent) || !text(b, hardware) || !text(b, firmware) || !text(b, "")) return {};
    return f;
}
Frame Protocol::open(const char *user, float hashrate) {
    if (state_ != 1 || !std::isfinite(hashrate) || hashrate < 0) return {};
    Frame f; f.type = 0x13; auto &b = f.payload; put(b, 1);
    if (!text(b, user)) return {};
    uint32_t bits; memcpy(&bits, &hashrate, 4); put(b, bits);
    b.insert(b.end(), 32, 255); // maximum target; the pool remains free to assign a harder one
    put(b, 8, 2); // request at least 8 bytes of unique local extranonce space
    return f;
}
Event Protocol::fail(const char *message) { error_ = message; return Event::Error; }
double Protocol::difficulty() const { return work::hashDifficulty(target_); }
void Protocol::activate(Slot &slot, uint32_t time, bool clean) {
    current_ = slot.job;
    current_.ntime = time; current_.nbits = nbits_; current_.clean = clean;
    memcpy(current_.prevHash, previous_, 32);
    haveCurrent_ = true; slot.future = false;
}
// An empty slot, else the oldest job that is neither the current one nor a
// future job: a pool may announce the job for the next block long before its
// SetNewPrevHash, with many template updates in between. Replacing it would
// leave that SetNewPrevHash pointing at nothing.
size_t Protocol::freeSlot() const {
    size_t best = jobs_.size();
    auto older = [&](size_t i) { return best == jobs_.size() || int32_t(jobs_[i].order - jobs_[best].order) < 0; };
    for (size_t i = 0; i < jobs_.size(); ++i) {
        const Slot &s = jobs_[i];
        if (!s.valid) return i;
        if (!s.future && !(haveCurrent_ && s.job.id == current_.id) && older(i)) best = i;
    }
    if (best < jobs_.size()) return best;
    for (size_t i = 0; i < jobs_.size(); ++i)  // only future jobs left besides the current one
        if (jobs_[i].future && older(i)) best = i;
    return best;
}
size_t Protocol::pendingIndex(uint32_t sequence) const {
    for (size_t i = 0; i < pendingCount_; ++i) if (pending_[i] == sequence) return i;
    return pendingCount_;
}
Frame Protocol::submit(const stratum::Share &s) {
    if (!opened() || pendingCount_ >= 32 || !haveCurrent_) return {};
    bool valid = false; uint32_t id = 0;
    for (const auto &slot : jobs_) if (slot.valid && !slot.future && slot.job.id == s.jobId && ((slot.job.version ^ s.version) & ~slot.job.versionMask) == 0) {
        if (!work::meetsTarget(s.hash, slot.job.shareTarget)) return {};
        valid = true; id = slot.id; break;
    }
    if (!valid) return {};
    std::vector<uint8_t> extranonce;
    if (!work::fromHex(s.extranonce2, extranonce) || extranonce.size() != extranonceSize_) return {};
    Frame f; f.type = 0x1b; f.extension = 0x8000; auto &b = f.payload;
    put(b, channel_); put(b, sequence_); put(b, id); put(b, s.nonce); put(b, s.ntime); put(b, s.version);
    b.push_back(extranonce.size()); b.insert(b.end(), extranonce.begin(), extranonce.end());
    pending_[pendingCount_++] = sequence_++; return f;
}
Event Protocol::handle(const Frame &f) {
    acknowledged_ = 0;
    if ((f.extension & 0x7fff) || f.payload.size() > kMaxPayload) return fail("Unsupported SV2 frame");
    Reader r{f.payload};
    if (f.type == 0x02) {
        r.number(4); error_ = r.text();
        if (!r.end()) return fail("Invalid SV2 setup error");
        return Event::Error;
    }
    if (f.type == 0x01) {
        uint32_t version = r.number(2), flags = r.number(4);
        if (!r.end() || version != 2 || state_ != 0 || (flags & ~3u)) return fail("Invalid SV2 setup response");
        state_ = 1; return Event::Setup;
    }
    if (f.type == 0x12) {
        uint32_t request = r.number(4); error_ = r.text();
        if (!r.end() || request != 1) return fail("Invalid SV2 channel error");
        return Event::Error;
    }
    if (f.type == 0x14) {
        uint32_t request = r.number(4), channel = r.number(4); uint8_t target[32]{}; r.bytes(target, 32);
        uint16_t size = r.number(2); auto prefix = r.vector(r.number(1)); uint32_t group = r.number(4);
        if (!r.end() || state_ != 1 || request != 1 || size < 8 || size > 32 || prefix.size() > 32 ||
            channel == group || zero(target, 32)) return fail("Invalid SV2 channel parameters");
        channel_ = channel; group_ = group; prefix_ = std::move(prefix); extranonceSize_ = size;
        memcpy(target_, target, 32); state_ = 2; return Event::Opened;
    }
    if (!opened()) return fail("SV2 channel not open");
    if (f.type == 0x1f) {
        uint32_t channel = r.number(4), id = r.number(4), option = r.number(1), time = option == 1 ? r.number(4) : 0;
        Slot slot; slot.valid = true; slot.id = id; slot.future = option == 0;
        slot.dynamicTarget = slot.future;
        slot.job.exactShareTarget = true; memcpy(slot.job.shareTarget, target_, 32);
        slot.job.difficulty = difficulty();
        slot.job.extranonce1 = prefix_; slot.job.extranonce2Size = extranonceSize_;
        slot.job.id = std::to_string(id); slot.job.version = r.number(4);
        uint32_t rolling = r.number(1), branches = r.number(1);
        slot.job.versionMask = rolling ? 0x1fffe000u : 0;
        if (!r.ok || option > 1 || rolling > 1 || branches > 32) return fail("Invalid SV2 job fields");
        for (uint32_t i = 0; i < branches; ++i) slot.job.branches.push_back(r.vector(32));
        slot.job.coinb1 = r.vector(r.number(2)); slot.job.coinb2 = r.vector(r.number(2));
        if (!r.end() || (channel != channel_ && channel != group_) || (!slot.future && (!havePrevious_ || time < ntime_)))
            return fail("Invalid SV2 mining job");
        size_t index = freeSlot();
        slot.order = order_++;
        size_t bytes = slot.job.coinb1.size() + slot.job.coinb2.size();
        for (const auto &old : jobs_) if (old.valid) {
            if (old.id == id) return fail("Duplicate SV2 job identifier");
            if (&old != &jobs_[index]) bytes += old.job.coinb1.size() + old.job.coinb2.size();
        }
        if (bytes > 32768) return fail("SV2 pending job memory limit exceeded");
        auto &dest = jobs_[index]; dest = std::move(slot);
        if (!dest.future) { activate(dest, time, false); return Event::Job; }
        return Event::None;
    }
    if (f.type == 0x20) {
        uint32_t channel = r.number(4), id = r.number(4); uint8_t prev[32]{}; r.bytes(prev, 32);
        uint32_t time = r.number(4), bits = r.number(4);
        if (!r.end() || (channel != channel_ && channel != group_) || !time || !bits) return fail("Invalid SV2 previous hash");
        Slot *chosen = nullptr;
        for (auto &s : jobs_) if (s.valid && s.future && s.id == id) chosen = &s;
        if (!chosen) return fail("Unknown SV2 future job");
        memcpy(previous_, prev, 32); ntime_ = time; nbits_ = bits; havePrevious_ = true;
        for (auto &s : jobs_) if (&s != chosen) s = Slot{};
        activate(*chosen, time, true); return Event::Job;
    }
    if (f.type == 0x21) {
        uint32_t channel = r.number(4); uint8_t target[32]{}; r.bytes(target, 32);
        if (!r.end() || (channel != channel_ && channel != group_) || zero(target, 32)) return fail("Invalid SV2 target");
        memcpy(target_, target, 32);
        // Explicit-ntime jobs retain the target assigned when received.
        // Future jobs follow the channel target even after activation.
        for (auto &slot : jobs_) if (slot.valid && slot.dynamicTarget) {
            memcpy(slot.job.shareTarget, target, 32); slot.job.difficulty = difficulty();
            if (haveCurrent_ && slot.job.id == current_.id) {
                memcpy(current_.shareTarget, target, 32); current_.difficulty = difficulty();
            }
        }
        if (haveCurrent_) current_.clean = false;
        return Event::Target;
    }
    if (f.type == 0x1c) {
        uint32_t channel = r.number(4), last = r.number(4), count = r.number(4);
        r.number(4); r.number(4); // new_shares_sum: U64, accounting only
        if (!r.end() || channel != channel_) return fail("Invalid SV2 share acknowledgment");
        if (pendingIndex(last) == pendingCount_) return Event::None; // duplicate/unknown acknowledgment
        size_t eligible = 0;
        for (size_t i = 0; i < pendingCount_; ++i) if (int32_t(pending_[i] - last) <= 0) ++eligible;
        if (!count || count != eligible) return fail("Invalid SV2 share acknowledgment count");
        size_t keep = 0;
        for (size_t i = 0; i < pendingCount_; ++i) if (int32_t(pending_[i] - last) > 0) pending_[keep++] = pending_[i];
        pendingCount_ = keep; acknowledged_ = count; return Event::Accepted;
    }
    if (f.type == 0x1d) {
        uint32_t channel = r.number(4), sequence = r.number(4); std::string error = r.text();
        if (!r.end() || channel != channel_) return fail("Invalid SV2 share rejection");
        size_t index = pendingIndex(sequence); if (index == pendingCount_) return Event::None;
        for (size_t i = index + 1; i < pendingCount_; ++i) pending_[i - 1] = pending_[i];
        --pendingCount_; acknowledged_ = 1; error_ = error; return Event::Rejected;
    }
    if (f.type == 0x18) {
        uint32_t channel = r.number(4); auto reason = r.text();
        if (!r.end()) return fail("Invalid SV2 channel closure");
        if (channel != channel_ && channel != group_) return Event::None;
        error_ = "SV2 channel closed: " + reason; return Event::Error;
    }
    if (f.type == 0x19) {
        uint32_t channel = r.number(4); auto prefix = r.vector(r.number(1));
        if (!r.end() || prefix.size() > 32) return fail("Invalid SV2 extranonce prefix");
        // Prefix changes apply only to subsequently received jobs. Group
        // prefixes are ignored; existing jobs retain their coinbase bytes.
        if (channel == channel_) prefix_ = std::move(prefix);
        return Event::None;
    }
    if (f.type == 0x25) {
        uint32_t group = r.number(4), count = r.number(2);
        if (!r.ok || count > (f.payload.size() - r.p) / 4) return fail("Invalid SV2 channel group");
        bool member = false;
        for (uint32_t i = 0; i < count; ++i) if (r.number(4) == channel_) member = true;
        if (!r.end() || group == channel_) return fail("Invalid SV2 channel group");
        if (member) group_ = group;
        return Event::None;
    }
    return Event::None; // optional messages do not mutate the current job
}
} // namespace sv2
