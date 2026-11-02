// Public telemetry uses a fixed group, never the configured hostname or URL.
#pragma once
#include <cstring>
#include <strings.h>

inline const char *publicPoolGroup(const char *host) {
    static const char *const domains[] = {"public-pool.io", "nerdminers.org", "nerdminer.io", "ckpool.org",
                                         "ocean.xyz", "braiins.com", "viabtc.com", "f2pool.com", "antpool.com"};
    if (!host) return "other";
    size_t length = strlen(host);
    if (length && host[length - 1] == '.') --length;
    if (!length || length > 253 || host[0] == '.') return "other";
    for (size_t i = 0; i < length; ++i) {
        char c = host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || (c == '.' && i && host[i - 1] != '.'))) return "other";
    }
    for (const char *domain : domains) {
        size_t n = strlen(domain);
        if (length >= n && (length == n || host[length - n - 1] == '.') &&
            !strncasecmp(host + length - n, domain, n)) return domain;
    }
    return "other";
}
