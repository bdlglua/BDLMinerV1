// Shared interpretation of SV2 URLs for persisted settings and HTTP validation.
#pragma once
#include <cctype>
#include <cstdint>
#include <string>

namespace stratum {
inline std::string trimIdentity(std::string text) {
    size_t first = 0, last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}
struct PoolIdentity { uint8_t protocol; std::string authority; };
inline PoolIdentity poolIdentity(const char *host, uint8_t protocol, const char *authority) {
    PoolIdentity result{protocol, trimIdentity(authority ? authority : "")};
    std::string url = trimIdentity(host ? host : "");
    if (url.compare(0, 15, "stratum2+tcp://") == 0) {
        result.protocol = 1;
        size_t path = url.find('/', 15);
        if (path != std::string::npos && path + 1 < url.size()) result.authority = trimIdentity(url.substr(path + 1));
    }
    return result;
}
} // namespace stratum
