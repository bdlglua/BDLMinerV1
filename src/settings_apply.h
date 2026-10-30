// Apply web/backup values to a staged copy; live network strings stay intact.
#pragma once

#include <ArduinoJson.h>
#include <cmath>
#include <cctype>
#include <cstring>
#include <utility>
#include "settings.h"
#ifdef BOARD_GAMMA601
#include "core/bm1370.h"
#endif
#include "core/sv2_noise.h"
#include "core/pool_identity.h"

struct SettingLimits { long minimum, maximum; };
inline SettingLimits settingLimits(const SettingInfo &s) {
    SettingLimits limits{0, s.bits == 8 ? 255 : 65535};
    if (!strcmp(s.key, "port") || !strcmp(s.key, "port2") || !strcmp(s.key, "mqttport")) limits.minimum = 1;
    if (!strcmp(s.key, "bright") || !strcmp(s.key, "power") || !strcmp(s.key, "alertpct")) limits.maximum = 100;
    if (!strcmp(s.key, "alertmin")) { limits.minimum = 1; limits.maximum = 240; }
    if (!strcmp(s.key, "rot")) limits.maximum = 3;
    if (!strcmp(s.key, "lang")) limits.maximum = 1;
    if (!strcmp(s.key, "theme")) limits.maximum = 4;
    if (!strcmp(s.key, "proto") || !strcmp(s.key, "proto2")) limits.maximum = 1;
    if (!strcmp(s.key, "update")) limits.maximum = 2;
#ifdef BOARD_GAMMA601
    if (!strcmp(s.key, "asicmhz")) { limits.minimum=bm1370::kStockMHz; limits.maximum=bm1370::kMaxMHz; }
#endif
    return limits;
}

// Validate the complete request before staging or persisting any field. Null,
// strings masquerading as numbers and blank numeric inputs must not disable
// safety settings or silently become zero.
inline const char *settingsError(JsonObjectConst values) {
    for (size_t i = 0; i < kSettingCount; i++) {
        const SettingInfo &s = kSettings[i];
        JsonVariantConst v = values[s.key];
        if (v.isUnbound()) continue;
        switch (s.type) {
            case SettingType::Text:
            case SettingType::Secret:
                if (!v.is<const char *>()) return "Invalid text setting";
                if (!strcmp(s.key, "ua") && strlen(v.as<const char *>()) > 64) return "User agent must be at most 64 characters";
                if (!strcmp(s.key, "hook")) {
                    const char *url = v.as<const char *>();
                    while (*url && std::isspace((unsigned char)*url)) ++url;
                    if (*url && strncmp(url, "http://", 7) && strncmp(url, "https://", 8))
                        return "The webhook URL must start with http:// or https://";
                }
                if (!strcmp(s.key, "pool") || !strcmp(s.key, "addr")) {
                    const char *text = v.as<const char *>();
                    while (*text && std::isspace((unsigned char)*text)) ++text;
                    if (!*text) return "Pool host and payout address cannot be empty";
                }
                break;
            case SettingType::Number: {
                SettingLimits limits = settingLimits(s);
                if (!v.is<long>() || v.as<long>() < limits.minimum || v.as<long>() > limits.maximum)
                    return "Setting outside its allowed range";
                long n = v.as<long>();
#ifdef BOARD_GAMMA601
                if (!strcmp(s.key, "asicmhz") && !bm1370::validProfile(unsigned(n)))
                    return "Gamma frequency must be 525-800 MHz in 25 MHz steps";
#endif
                if (!strcmp(s.key, "maxtemp") && n && n < 30) return "Temperature must be 0 or at least 30 C";
                if ((!strcmp(s.key, "nightfrom") || !strcmp(s.key, "nightto")) && n > 23 && n != 255)
                    return "Night hours must be 0-23 or 255";
                if (!strcmp(s.key, "summary") && n > 23 && n != 255) return "The summary hour must be 0-23 or 255";
                break;
            }
            case SettingType::Decimal:
                if (!v.is<double>() || !std::isfinite(v.as<double>()) || v.as<double>() <= 0)
                    return "Difficulty must be a finite positive number";
                break;
            case SettingType::Bool:
                if (!v.is<bool>()) return "Invalid checkbox setting";
                break;
        }
    }
    bool automatic = values["uaauto"] | settings.userAgentAuto;
    const char *agent = values["ua"] | settings.userAgent.c_str();
    if (!automatic) {
        while (*agent && std::isspace(static_cast<unsigned char>(*agent))) ++agent;
        if (!*agent) return "A manual user agent is required";
    }
    for (const auto &fields : {std::pair<const char *, const char *>{"proto", "sv2key"}, {"proto2", "sv2key2"}}) {
        unsigned protocol = values[fields.first] | unsigned(!strcmp(fields.first, "proto") ? settings.poolProtocol : settings.backupProtocol);
        const char *key = values[fields.second] | (!strcmp(fields.first, "proto") ? settings.poolAuthority.c_str() : settings.backupAuthority.c_str());
        const bool main = !strcmp(fields.first, "proto");
        const char *host = values[main ? "pool" : "pool2"] | (main ? settings.poolHost.c_str() : settings.backupHost.c_str());
        auto identity = stratum::poolIdentity(host, protocol, key);
        uint8_t authority[32];
        if (identity.protocol == 1 && !sv2::authorityKey(identity.authority.c_str(), authority)) return "A valid Stratum V2 authority key is required";
    }
    return nullptr;
}

inline int settingsApply(Settings &pending, JsonObjectConst values) {
    int changed = 0;
    for (size_t i = 0; i < kSettingCount; i++) {
        const SettingInfo &s = kSettings[i];
        JsonVariantConst v = values[s.key];
        if (v.isNull()) continue;
        void *value = settingValue(s, pending);
        switch (s.type) {
            case SettingType::Text:
            case SettingType::Secret: *(String *)value = v.as<String>(); break;
            case SettingType::Number: {
                long n = v.as<long>();
                long maximum = s.bits == 8 ? 255 : 65535;
                n = n < 0 ? 0 : n > maximum ? maximum : n;
                if (s.bits == 8) *(uint8_t *)value = n;
                else *(uint16_t *)value = n;
                break;
            }
            case SettingType::Decimal: *(double *)value = v.as<double>(); break;
            case SettingType::Bool: *(bool *)value = v.as<bool>(); break;
        }
        changed++;
    }
    if (values["uaauto"].isUnbound() && values["ua"].is<const char *>())
        pending.userAgentAuto = !legacyManualAgent(values["ua"].as<const char *>());
    return changed;
}

// Settings applied while the miner runs: numbers and switches that are read
// where they are used (supervision, screen, alerts, updates). Text settings,
// the pool, WiFi and MQTT rebuild connections or tasks: they need a restart,
// and other tasks read those strings without a lock.
inline bool settingIsLive(const char *key) {
    static const char *const kLive[] = {"bright", "rot",      "autorot",  "lang",     "theme",  "cycle",
                                        "nightfrom", "nightto", "beep",   "notify32", "alertpct", "alertmin", "summary",
                                        "maxtemp", "power",    "rebooth",  "saver",    "update", "count", "sharehr"};
    for (const char *k : kLive)
        if (!strcmp(k, key)) return true;
    return false;
}

inline bool settingDiffers(const SettingInfo &s, const Settings &a, const Settings &b) {
    const void *x = settingValue(s, a), *y = settingValue(s, b);
    switch (s.type) {
        case SettingType::Text:
        case SettingType::Secret: return *(const String *)x != *(const String *)y;
        case SettingType::Number:
            return s.bits == 8 ? *(const uint8_t *)x != *(const uint8_t *)y : *(const uint16_t *)x != *(const uint16_t *)y;
        case SettingType::Decimal: return *(const double *)x != *(const double *)y;
        case SettingType::Bool: return *(const bool *)x != *(const bool *)y;
    }
    return false;
}

// How `pending` differs from `current`: the number of changed settings, and
// whether all of them can be applied without a restart.
struct SettingsChange { int count = 0; bool live = true; };
inline SettingsChange settingsCompare(const Settings &pending, const Settings &current) {
    SettingsChange change;
    for (size_t i = 0; i < kSettingCount; i++) {
        if (!settingDiffers(kSettings[i], pending, current)) continue;
        change.count++;
        if (!settingIsLive(kSettings[i].key)) change.live = false;
    }
    return change;
}

// Copies the live settings that changed (numbers and switches only).
inline void settingsCopyLive(Settings &live, const Settings &pending) {
    for (size_t i = 0; i < kSettingCount; i++) {
        const SettingInfo &s = kSettings[i];
        if (!settingIsLive(s.key) || s.type == SettingType::Text || s.type == SettingType::Secret) continue;
        size_t size = s.type == SettingType::Number ? s.bits / 8 : s.type == SettingType::Decimal ? sizeof(double) : sizeof(bool);
        memcpy(settingValue(s, live), settingValue(s, pending), size);
    }
}
