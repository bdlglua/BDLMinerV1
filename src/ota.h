// A single owner for the global Arduino Update object, across every task.
#pragma once

#include <atomic>
#include <cstdint>

namespace ota {

enum class Owner : uint8_t { None, Web, Github, Restart };
inline std::atomic<Owner> &sessionOwner() {
    static std::atomic<Owner> owner{Owner::None};
    return owner;
}
inline bool acquire(Owner who) {
    if (who == Owner::None) return false;
    Owner expected = Owner::None;
    return sessionOwner().compare_exchange_strong(expected, who);
}
inline bool owns(Owner who) { return sessionOwner().load() == who; }
inline bool busy() { return !owns(Owner::None); }
inline void release(Owner who) {
    sessionOwner().compare_exchange_strong(who, Owner::None);
}

class Guard {
    Owner who;
    bool acquired;
public:
    explicit Guard(Owner value) : who(value), acquired(acquire(value)) {}
    ~Guard() { if (acquired) release(who); }
    explicit operator bool() const { return acquired; }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
};

}  // namespace ota
