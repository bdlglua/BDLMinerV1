// Apply web/backup values to a staged copy; live network strings stay intact.
#pragma once

#include <ArduinoJson.h>
#include "settings.h"

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
    return changed;
}
