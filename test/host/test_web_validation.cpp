#include <assert.h>
#include <limits>
#include <stdio.h>

#include "web_validation.h"

static const char *kBackup = R"({"bdlminer_backup":1,"settings":{"pool":"example.org"},
    "stats":{"best":12.5,"hashes":18446744073709551615,"shares":3,"blocks":0,
    "seconds":100,"top":[{"d":12.5,"t":0},{"d":1,"t":1700000000}]}})";

static bool valid(JsonDocument &j) {
    return webvalidation::backupError(j.as<JsonObjectConst>(), 10) == nullptr;
}

int main() {
    JsonDocument j;
    assert(!deserializeJson(j, kBackup));
    assert(valid(j));
    assert(j["stats"]["hashes"].as<uint64_t>() == UINT64_MAX);

    // The old settings-only format remains accepted.
    assert(!deserializeJson(j, R"({"pool":"example.org","port":3333})"));
    assert(valid(j));

    for (const char *version : {"null", "0", "2", "true", "\"1\""}) {
        assert(!deserializeJson(j, kBackup));
        JsonDocument v;
        assert(!deserializeJson(v, version));
        j["bdlminer_backup"].set(v.as<JsonVariantConst>());
        assert(!valid(j));
    }
    for (const char *key : {"settings", "stats"}) {
        assert(!deserializeJson(j, kBackup));
        j.remove(key);
        assert(!valid(j));
    }
    // A truncated stats object must never silently reset missing totals to 0.
    for (const char *key : {"best", "hashes", "shares", "blocks", "seconds", "top"}) {
        assert(!deserializeJson(j, kBackup));
        j["stats"].remove(key);
        assert(!valid(j));
    }
    for (const char *key : {"hashes", "shares", "blocks", "seconds"}) {
        for (const char *value : {"-1", "1.5", "\"12\"", "true", "null"}) {
            assert(!deserializeJson(j, kBackup));
            JsonDocument v;
            assert(!deserializeJson(v, value));
            j["stats"][key].set(v.as<JsonVariantConst>());
            assert(!valid(j));
        }
    }
    assert(!deserializeJson(j, kBackup));
    j["stats"]["shares"] = (uint64_t)UINT32_MAX + 1;
    assert(!valid(j));
    for (double value : {-1.0, std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()}) {
        assert(!deserializeJson(j, kBackup));
        j["stats"]["best"] = value;
        assert(!valid(j));
    }
    for (const char *top : {"{}", "[null]", "[{\"d\":1}]", "[{\"d\":0,\"t\":0}]"}) {
        assert(!deserializeJson(j, kBackup));
        JsonDocument v;
        assert(!deserializeJson(v, top));
        j["stats"]["top"].set(v.as<JsonVariantConst>());
        assert(!valid(j));
    }
    assert(!deserializeJson(j, kBackup));
    for (int i = 0; i < 9; i++) {
        JsonObject entry = j["stats"]["top"].as<JsonArray>().add<JsonObject>();
        entry["d"] = 1;
        entry["t"] = 0;
    }
    assert(!valid(j));
    assert(!deserializeJson(j, kBackup));
    j["stats"]["best"] = 0;
    j["stats"]["top"].to<JsonArray>();
    assert(valid(j));

    // ArduinoJson's float/double serialization can reverse the order of
    // very close difficulties. A real exported backup must remain readable.
    assert(!deserializeJson(j, kBackup));
    j["stats"]["best"] = 0.056910570710897453;
    j["stats"]["top"][0]["d"] = 0.056910570710897453;
    j["stats"]["top"][1]["d"] = 0.056910570710897446;
    std::string exported;
    serializeJson(j, exported);
    assert(!deserializeJson(j, exported));
    assert(valid(j));
    puts("Backup validation tests passed");
}
