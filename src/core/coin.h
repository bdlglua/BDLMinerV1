// Supported chains share SHA-256d. A pool supplies the actual chain's jobs;
// choosing a profile cannot change the chain served by an arbitrary endpoint.
#pragma once
#include <cstdint>
#include <string>
#include <algorithm>
#include <cctype>
#include "public_pool.h"

namespace coin {
enum Id : uint8_t { BTC = 0, BCH = 1, Count };
inline bool valid(unsigned id) { return id < Count; }
inline const char *ticker(unsigned id) { return id == BCH ? "BCH" : "BTC"; }
inline const char *name(unsigned id) { return id == BCH ? "Bitcoin Cash" : "Bitcoin"; }
inline bool bitcoinData(unsigned id) { return id == BTC; }
inline bool addressCompatible(unsigned id, std::string user) {
    user = user.substr(0, user.find('.'));
    std::transform(user.begin(), user.end(), user.begin(), [](unsigned char c) { return std::tolower(c); });
    if (id == BCH) return user.compare(0, 3, "bc1") && user.compare(0, 3, "tb1") && user.compare(0, 5, "bcrt1");
    return user.compare(0, 12, "bitcoincash:") && user.compare(0, 8, "bchtest:") && user.compare(0, 7, "bchreg:");
}

// Reject known Bitcoin-only services when changing chain. Custom endpoints
// remain the operator's responsibility; Stratum V1 has no chain identifier.
inline bool poolCompatible(unsigned id, const char *endpoint) {
    std::string host = endpoint ? endpoint : "";
    auto scheme = host.find("://");
    if (scheme != std::string::npos) host.erase(0, scheme + 3);
    host = host.substr(0, host.find_first_of(":/"));
    host.erase(std::remove_if(host.begin(), host.end(), [](unsigned char c) { return std::isspace(c); }), host.end());
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return std::tolower(c); });
    if (!host.empty() && host.back() == '.') host.pop_back();
    if (id == BTC) return host != "b4c.f2pool.com";
    if (id != BCH) return false;
    const char *group = publicPoolGroup(host.c_str());
    for (const char *btcOnly : {"public-pool.io", "nerdminers.org", "nerdminer.io", "ckpool.org", "ocean.xyz", "braiins.com"})
        if (!strcmp(group, btcOnly)) return false;
    return true;
}
inline const char *poolError(unsigned id, const char *host, unsigned protocol) {
    if (!valid(id)) return "Unsupported cryptocurrency";
    if (id != BTC && protocol != 0) return "BCH requires Stratum V1";
    if (!poolCompatible(id, host)) return "Choose a pool for the selected cryptocurrency";
    return nullptr;
}
} // namespace coin
