// Validation shared with host tests. Check a backup completely before any
// settings or lifetime statistics are written to flash.
#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <stdint.h>

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

}  // namespace webvalidation
