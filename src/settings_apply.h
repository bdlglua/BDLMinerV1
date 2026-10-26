// Apply web/backup values to a staged copy; live network strings stay intact.
#pragma once

#include <ArduinoJson.h>
#include <cmath>
#include <cctype>
#include <cstring>
#include <utility>
#include "settings.h"
#include "core/sv2_noise.h"

struct SettingLimits { long minimum, maximum; };
inline SettingLimits settingLimits(const SettingInfo &s) {
    SettingLimits limits{0, s.bits == 8 ? 255 : 65535};
    if (!strcmp(s.key, "port") || !strcmp(s.key, "port2") || !strcmp(s.key, "mqttport")) limits.minimum = 1;
    if (!strcmp(s.key, "bright") || !strcmp(s.key, "power")) limits.maximum = 100;
    if (!strcmp(s.key, "rot")) limits.maximum = 3;
    if (!strcmp(s.key, "lang")) limits.maximum = 1;
    if (!strcmp(s.key, "theme")) limits.maximum = 4;
    if (!strcmp(s.key, "proto") || !strcmp(s.key, "proto2")) limits.maximum = 1;
    if (!strcmp(s.key, "update")) limits.maximum = 2;
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
                if (!strcmp(s.key, "maxtemp") && n && n < 30) return "Temperature must be 0 or at least 30 C";
                if ((!strcmp(s.key, "nightfrom") || !strcmp(s.key, "nightto")) && n > 23 && n != 255)
                    return "Night hours must be 0-23 or 255";
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
        uint8_t authority[32];
        if (protocol == 1 && !sv2::authorityKey(key, authority)) return "A valid Stratum V2 authority key is required";
    }
    return nullptr;
}

inline int settingsApply(Settings &pending, JsonObjectConst values) {
    int changed = 0;
    for (size_t i = 0; i < kSettingCount; i++) {
        const SettingInfo &s = kSettings[i];
        JsonVariantConst v = values[s.key];
        if (v.isNull()) continue;
        if (s.value == &settings.webPassword && v.as<String>().length() == 0) continue;
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
