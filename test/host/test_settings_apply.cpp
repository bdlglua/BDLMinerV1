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
    {"maxtemp", SettingType::Number, &settings.maxTemp, 8, "", ""},
    {"nightfrom", SettingType::Number, &settings.nightFrom, 8, "", ""},
};
const size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);

int main() {
    settings.poolHost = "original.pool";
    settings.webPassword = "original-password";
    Settings pending = settings;
    JsonDocument changes;
    assert(!deserializeJson(changes, R"({"pool":"next.pool","port":3333,"power":40,"webpass":"next-password","diff":0.5,"saver":true})"));
    assert(settingsApply(pending, changes.as<JsonObjectConst>()) == 6);
    assert(settingsError(changes.as<JsonObjectConst>()) == nullptr);
    assert(pending.poolHost == "next.pool" && pending.poolPort == 3333);
    assert(pending.miningPower == 40 && pending.webPassword == "next-password");
    assert(pending.suggestedDifficulty == 0.5 && pending.batterySaver);
    assert(settings.poolHost == "original.pool" && settings.poolPort == DEFAULT_POOL_PORT);
    assert(settings.miningPower == 100 && settings.webPassword == "original-password");
    assert(settings.suggestedDifficulty == DEFAULT_DIFFICULTY && !settings.batterySaver);

    // Empty passwords keep access; partial backups leave other fields intact.
    assert(!deserializeJson(changes, R"({"webpass":"","power":-5,"port":100000})"));
    assert(settingsApply(pending, changes.as<JsonObjectConst>()) == 2);
    assert(pending.webPassword == "next-password" && pending.poolHost == "next.pool");
    assert(pending.miningPower == 0 && pending.poolPort == 65535);
    assert(settings.webPassword == "original-password");

    // HTTP handlers validate every field before applying anything. Invalid
    // numeric/checkbox inputs must not silently become zero or true.
    for (const char *raw : {R"({"power":null})", R"({"power":""})", R"({"power":"25"})",
                           R"({"power":101})", R"({"power":-1})", R"({"power":12.5})",
                           R"({"port":0})", R"({"port":65536})", R"({"diff":0})",
                           R"({"diff":-1})", R"({"saver":"false"})", R"({"pool":null})",
                           R"({"pool":"   "})", R"({"maxtemp":25})", R"({"nightfrom":24})"}) {
        assert(!deserializeJson(changes, raw));
        assert(settingsError(changes.as<JsonObjectConst>()) != nullptr);
    }
    assert(!deserializeJson(changes, R"({"power":0,"port":65535,"diff":0.000001,"saver":false,"maxtemp":0,"nightfrom":255})"));
    assert(settingsError(changes.as<JsonObjectConst>()) == nullptr);
    puts("Staged settings preserve live network strings and clamp numeric fields: passed");
}
