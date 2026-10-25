#include <cassert>
#include <cstdio>
#include "settings_apply.h"

Settings settings;
const SettingInfo kSettings[] = {
    {"pool", SettingType::Text, &settings.poolHost, 0, "", ""},
    {"port", SettingType::Number, &settings.poolPort, 16, "", ""},
    {"power", SettingType::Number, &settings.miningPower, 8, "", ""},
    {"webpass", SettingType::Secret, &settings.webPassword, 0, "", ""},
    {"diff", SettingType::Decimal, &settings.suggestedDifficulty, 0, "", ""},
    {"saver", SettingType::Bool, &settings.batterySaver, 0, "", ""},
};
const size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);

int main() {
    settings.poolHost = "original.pool";
    settings.webPassword = "original-password";
    Settings pending = settings;
    JsonDocument changes;
    assert(!deserializeJson(changes, R"({"pool":"next.pool","port":3333,"power":40,"webpass":"next-password","diff":0.5,"saver":true})"));
    assert(settingsApply(pending, changes.as<JsonObjectConst>()) == 6);
    assert(pending.poolHost == "next.pool" && pending.poolPort == 3333);
    assert(pending.miningPower == 40 && pending.webPassword == "next-password");
    assert(pending.suggestedDifficulty == 0.5 && pending.batterySaver);
    assert(settings.poolHost == "original.pool" && settings.poolPort == DEFAULT_POOL_PORT);
    assert(settings.miningPower == 75 && settings.webPassword == "original-password");
    assert(settings.suggestedDifficulty == DEFAULT_DIFFICULTY && !settings.batterySaver);

    // Empty passwords keep access; partial backups leave other fields intact.
    assert(!deserializeJson(changes, R"({"webpass":"","power":-5,"port":100000})"));
    assert(settingsApply(pending, changes.as<JsonObjectConst>()) == 2);
    assert(pending.webPassword == "next-password" && pending.poolHost == "next.pool");
    assert(pending.miningPower == 0 && pending.poolPort == 65535);
    assert(settings.webPassword == "original-password");
    puts("Staged settings preserve live network strings and clamp numeric fields: passed");
}
