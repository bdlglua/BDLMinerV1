// Validation shared with host tests. Check a backup completely before any
// settings or lifetime statistics are written to flash.
#pragma once

#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace webvalidation {

inline bool nonnegativeNumber(JsonVariantConst v) {
    return v.is<double>() && isfinite(v.as<double>()) && v.as<double>() >= 0;
}

inline const char *backupError(JsonObjectConst backup, size_t maxTopShares) {
    // Settings-only exports from earlier firmware have no format marker.
    if (backup["bdlminer_backup"].isUnbound()) return nullptr;
    if (!backup["bdlminer_backup"].is<unsigned>() || backup["bdlminer_backup"].as<unsigned>() != 1)
        return "Unsupported backup version";
    if (!backup["settings"].is<JsonObjectConst>()) return "Invalid backup settings";
    if (!backup["stats"].is<JsonObjectConst>()) return "Missing backup statistics";
    JsonObjectConst st = backup["stats"];
    if (!nonnegativeNumber(st["best"]) || !st["hashes"].is<uint64_t>() ||
        !st["shares"].is<uint32_t>() || !st["blocks"].is<uint32_t>() || !st["seconds"].is<uint32_t>() ||
        !st["top"].is<JsonArrayConst>())
        return "Invalid or incomplete backup statistics";
    JsonArrayConst top = st["top"];
    if (top.size() > maxTopShares) return "Too many best shares in backup";
    // JSON serialization can round nearby difficulties differently. Do not
    // reject an exported backup because this slightly changes their ordering.
    for (JsonVariantConst entry : top) {
        if (!entry.is<JsonObjectConst>() || !nonnegativeNumber(entry["d"]) ||
            entry["d"].as<double>() <= 0 || !entry["t"].is<uint32_t>())
            return "Invalid best share in backup";
    }
    return nullptr;
}

inline bool sameText(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

// The Host header of a request that changes the miner or reads its secrets.
// A name another site controls could lead to the miner (DNS rebinding): only
// an IP address, a name without a dot, a local name (.local, .lan...) or the
// miner's own name (bdlminer-xxxx, whatever follows it) are accepted.
inline bool hostAllowed(const char *host, const char *ownName) {
    if (!host || !*host || *host == '[') return true;  // no header (not a browser) or an IPv6 address
    size_t n = strcspn(host, ":");
    if (n && host[n - 1] == '.') n--;
    if (strspn(host, "0123456789.") >= n) return true;  // IPv4
    const char *dot = (const char *)memchr(host, '.', n);
    if (!dot) return true;
    size_t own = strlen(ownName);
    if (own && (size_t)(dot - host) == own && sameText(host, ownName, own)) return true;
    static const char *kLocal[] = {".local", ".lan", ".home", ".home.arpa", ".internal", ".localdomain"};
    for (const char *suffix : kLocal) {
        size_t s = strlen(suffix);
        if (n > s && sameText(host + n - s, suffix, s)) return true;
    }
    return false;
}

// The Origin header, when the browser sends one (every cross-site POST has
// it), must be this very miner: another site's page cannot use it.
inline bool originAllowed(const char *origin, const char *host) {
    if (!origin || !*origin) return true;
    if (strncmp(origin, "http://", 7)) return false;
    origin += 7;
    size_t n = strlen(origin);
    return host && strlen(host) == n && sameText(origin, host, n);
}

}  // namespace webvalidation
