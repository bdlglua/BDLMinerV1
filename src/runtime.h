// Failed task/allocation starts stay visible in diagnostics instead of
// reporting engines or services that never actually started.
#pragma once
#include <atomic>
#include <cstdint>
#include "log.h"

namespace runtime {
enum Task : uint16_t { Miner = 1, Network = 2, Market = 4, Mqtt = 8, Notify = 16, Web = 32 };
inline std::atomic<uint16_t> &failures() {
    static std::atomic<uint16_t> value{0};
    return value;
}
inline bool started(bool ok, Task task, const char *name) {
    if (!ok) {
        failures().fetch_or(task);
        logf("[runtime] could not start %s: not enough memory\n", name);
    }
    return ok;
}
}  // namespace runtime
