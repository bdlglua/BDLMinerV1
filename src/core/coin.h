// Supported chains share SHA-256d. A pool supplies the actual chain's jobs;
// choosing a profile cannot change the chain served by an arbitrary endpoint.
#pragma once
#include <cstdint>
#include <string>
#include <algorithm>
#include <cctype>
#include "public_pool.h"

namespace coin {
// Append IDs: existing records keep BTC=0 and BCH=1.
enum Id : uint8_t { BTC = 0, BCH = 1, BSV = 2, XEC = 3, DGB = 4, Count };
inline bool valid(unsigned id) { return id < Count; }
inline const char *ticker(unsigned id) {
    static const char *const values[] = {"BTC", "BCH", "BSV", "XEC", "DGB"};
    return valid(id) ? values[id] : "unknown";
}
inline const char *name(unsigned id) {
    static const char *const values[] = {"Bitcoin", "Bitcoin Cash", "Bitcoin SV", "eCash", "DigiByte"};
    return valid(id) ? values[id] : "Unknown";
}
inline unsigned fromTicker(const char *value) {
    for (unsigned id = 0; id < Count; ++id) if (value && !std::strcmp(value, ticker(id))) return id;
    return Count;
}
inline bool bitcoinData(unsigned id) { return id == BTC; }
inline unsigned halvingInterval(unsigned id) { return valid(id) && id != DGB ? 210000 : 0; }
inline const char *cashPrefix(unsigned id, unsigned network = 0) {
    static const char *const bch[] = {"bitcoincash", "bchtest", "bchreg"};
    static const char *const xec[] = {"ecash", "ectest", "ecregtest"};
    return network < 3 ? (id == BCH ? bch[network] : id == XEC ? xec[network] : nullptr) : nullptr;
}
inline bool jobCompatible(unsigned id, uint32_t version, uint32_t rollingMask) {
    // DigiByte Core BLOCK_VERSION_ALGO = 15 << 8; SHA-256d is zero.
    // Algorithm bits must also be excluded from version rolling.
    return valid(id) && (id != DGB || !((version | rollingMask) & 0x00000f00u));
}
inline bool addressCompatible(unsigned id, std::string user) {
    user = user.substr(0, user.find('.'));
    std::transform(user.begin(), user.end(), user.begin(), [](unsigned char c) { return std::tolower(c); });
    for (unsigned cash : {unsigned(BCH), unsigned(XEC)}) for (unsigned net = 0; net < 3; ++net) {
        std::string prefix = std::string(cashPrefix(cash, net)) + ":";
        if (!user.compare(0, prefix.size(), prefix)) return id == cash;
    }
    for (const char *prefix : {"bc1", "tb1", "bcrt1"}) if (!user.compare(0, std::strlen(prefix), prefix)) return id == BTC;
    for (const char *prefix : {"dgb1", "dgbt1", "dgbrt1"}) if (!user.compare(0, std::strlen(prefix), prefix)) return id == DGB;
    return valid(id); // Legacy encodings can overlap; pools may use account names.
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
    if (!valid(id)) return false;
    struct Endpoint { const char *host; Id id; };
    static const Endpoint endpoints[] = {
        {"b4c.f2pool.com", BCH}, {"bch.hmpool.io", BCH}, {"xec.hmpool.io", XEC},
        {"bch.solopool.org", BCH}, {"xec.solopool.org", XEC}, {"bsv.solopool.org", BSV},
        {"dgb.solopool.org", DGB}, {"bsv.molepool.com", BSV}, {"xec.molepool.com", XEC}};
    for (const auto &endpoint : endpoints) if (host == endpoint.host) return id == endpoint.id;
    if (id == BTC) return true;
    const char *group = publicPoolGroup(host.c_str());
    for (const char *btcOnly : {"public-pool.io", "nerdminers.org", "nerdminer.io", "ckpool.org", "ocean.xyz", "braiins.com"})
        if (!strcmp(group, btcOnly)) return false;
    return true;
}
inline const char *poolError(unsigned id, const char *host, unsigned protocol) {
    if (!valid(id)) return "Unsupported cryptocurrency";
    if (id != BTC && protocol != 0) return "This cryptocurrency requires Stratum V1";
    if (!poolCompatible(id, host)) return "Choose a pool for the selected cryptocurrency";
    return nullptr;
}
} // namespace coin
