// Automatic Stratum identity, learned separately for each pool endpoint.
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
        // Always start with the native identity, including ESP32 pools.
        // Compatibility is learned only from an explicit agent refusal.
    }

    const char *select(const char *native, bool automatic = true, const char *manual = nullptr) const {
        return automatic ? (fallback_ ? compatible() : native) : manual ? manual : native;
    }

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
