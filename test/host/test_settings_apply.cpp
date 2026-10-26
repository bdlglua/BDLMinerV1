#include <cassert>
#include <cstdio>
#include "settings_apply.h"

Settings settings;
const SettingInfo kSettings[] = {
    {"pool", SettingType::Text, &settings.poolHost, 0, "", ""},
    {"pool2", SettingType::Text, &settings.backupHost, 0, "", ""},
    {"port", SettingType::Number, &settings.poolPort, 16, "", ""},
    {"power", SettingType::Number, &settings.miningPower, 8, "", ""},
    {"webpass", SettingType::Secret, &settings.webPassword, 0, "", ""},
    {"diff", SettingType::Decimal, &settings.suggestedDifficulty, 0, "", ""},
    {"saver", SettingType::Bool, &settings.batterySaver, 0, "", ""},
    {"maxtemp", SettingType::Number, &settings.maxTemp, 8, "", ""},
    {"nightfrom", SettingType::Number, &settings.nightFrom, 8, "", ""},
    {"uaauto", SettingType::Bool, &settings.userAgentAuto, 0, "", ""},
    {"ua", SettingType::Text, &settings.userAgent, 0, "", ""},
    {"proto", SettingType::Number, &settings.poolProtocol, 8, "", ""},
    {"proto2", SettingType::Number, &settings.backupProtocol, 8, "", ""},
    {"sv2key", SettingType::Text, &settings.poolAuthority, 0, "", ""},
    {"sv2key2", SettingType::Text, &settings.backupAuthority, 0, "", ""},
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
    for(const char *raw:{R"({"proto":2})",R"({"proto":1})",R"({"proto2":1,"sv2key2":"bad"})",R"({"uaauto":false,"ua":"   "})",R"({"uaauto":"true"})",
                        R"({"proto":1,"sv2key":"9c4zpyJ2ndm4e8sP2uNc1VNCGxYjqaxWS6wUCjk8zFj6njFquH7"})"}){
        assert(!deserializeJson(changes,raw));assert(settingsError(changes.as<JsonObjectConst>())!=nullptr);
    }
    assert(!deserializeJson(changes,R"({"proto":1,"sv2key":"9c4zpyJ2ndm4e8sP2uNc1VNCGxYjqaxWS6wUCjk8zFj6njFquH6","proto2":0,"uaauto":false,"ua":"CustomMiner"})"));
    assert(settingsError(changes.as<JsonObjectConst>())==nullptr);
    assert(settingsApply(pending,changes.as<JsonObjectConst>())==5);
    assert(pending.poolProtocol==1&&pending.backupProtocol==0&&!pending.userAgentAuto&&pending.userAgent=="CustomMiner");
    assert(settings.poolProtocol==0&&settings.userAgentAuto);
    // Pasted V2 URLs select V2 during sanitization. Validate that effective
    // protocol/key too, including backup URLs and whitespace around keys.
    for (const char *raw : {R"({"pool":"stratum2+tcp://public-pool.io:23330","proto":0})",
                           R"({"pool2":"stratum2+tcp://public-pool.io:23330/bad","proto2":0})"}) {
        assert(!deserializeJson(changes,raw)); assert(settingsError(changes.as<JsonObjectConst>())!=nullptr);
    }
    assert(!deserializeJson(changes,R"({"pool":"stratum2+tcp://public-pool.io:23330/9c4zpyJ2ndm4e8sP2uNc1VNCGxYjqaxWS6wUCjk8zFj6njFquH6","proto":1,"sv2key":""})"));
    assert(settingsError(changes.as<JsonObjectConst>())==nullptr);
    assert(!deserializeJson(changes,R"({"proto":1,"sv2key":" 9c4zpyJ2ndm4e8sP2uNc1VNCGxYjqaxWS6wUCjk8zFj6njFquH6  "})"));
    assert(settingsError(changes.as<JsonObjectConst>())==nullptr);
    puts("V1/V2 and automatic/manual settings: validation, staged isolation and preserved manual identity passed");
}
