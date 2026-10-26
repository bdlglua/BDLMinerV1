// Automatic Stratum V1 identity, learned separately for each pool endpoint.
#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

namespace stratum {

class PoolAgent {
public:
    static constexpr const char *compatible() { return "NerdMinerV2"; }

    void endpoint(const char *host, uint16_t port) {
        std::string name = host ? host : "";
        for (char &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!name.empty() && name.back() == '.') name.pop_back();
        if (name == host_ && port == port_) return;
        host_ = name;
        port_ = port;
        fallback_ = false;
        // These endpoints serve ESP32/NerdMiner work. Use their compatible
        // identity on the first attempt, without a failed subscription first.
        static const char *const pools[] = {
            "pool.nerdminers.org", "pool.nerdminer.io", "pool.bitronics.store",
            "pool.bitronics.com", "pool.solomining.de"
        };
        for (const char *pool : pools) if (host_ == pool) fallback_ = true;
    }

    const char *select(const char *native) const { return fallback_ ? compatible() : native; }

    bool refused(const char *error) {
        // Retry only an explicit agent refusal, once. Authentication errors,
        // network failures and unrelated refusals must retain their meaning.
        if (fallback_ || !error) return false;
        for (const char *p = error; *p; ++p) {
            const char *a = p, *b = "agent";
            while (*a && *b && std::tolower(static_cast<unsigned char>(*a)) == *b) { ++a; ++b; }
            if (!*b) { fallback_ = true; return true; }
        }
        return false;
    }

private:
    std::string host_;
    uint16_t port_ = 0;
    bool fallback_ = false;
};

}  // namespace stratum
