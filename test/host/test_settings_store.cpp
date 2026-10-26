#include <cassert>
#include <cstdio>
#include <map>
#include "settings_store.h"
Settings settings;
const SettingInfo kSettings[] = {
    {"pool", SettingType::Text, &settings.poolHost, 0, "", ""},
    {"power", SettingType::Number, &settings.miningPower, 8, "", ""},
    {"webpass", SettingType::Secret, &settings.webPassword, 0, "", ""},
    {"diff", SettingType::Decimal, &settings.suggestedDifficulty, 0, "", ""},
    {"saver", SettingType::Bool, &settings.batterySaver, 0, "", ""},
    {"uaauto", SettingType::Bool, &settings.userAgentAuto, 0, "", ""},
    {"ua", SettingType::Text, &settings.userAgent, 0, "", ""},
    {"proto", SettingType::Number, &settings.poolProtocol, 8, "", ""},
    {"proto2", SettingType::Number, &settings.backupProtocol, 8, "", ""},
    {"sv2key", SettingType::Text, &settings.poolAuthority, 0, "", ""},
    {"sv2key2", SettingType::Text, &settings.backupAuthority, 0, "", ""},
};
const size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);
struct Storage {
    std::map<std::string, std::vector<uint8_t>> bytes;
    int fault = 0;
    size_t getBytesLength(const char *key) { return bytes[key].size(); }
    size_t getBytes(const char *key, void *out, size_t size) {
        if (bytes[key].size() != size) return 0;
        std::memcpy(out, bytes[key].data(), size); return size;
    }
    size_t putBytes(const char *key, const void *data, size_t size) {
        if (fault == 1) return 0;
        auto start = static_cast<const uint8_t *>(data);
        bytes[key].assign(start, start + (fault == 2 ? size / 2 : size));
        if (fault == 3) bytes[key].back() ^= 1;
        return fault == 2 ? size / 2 : size;
    }
};
int main() {
    Storage storage;
    uint32_t sequence = 0;
    Settings original; original.poolHost = "original.pool"; original.miningPower = 75;
    original.webPassword = "secret";
    assert(settingsstore::save(storage, original, sequence) && sequence == 1);
    Settings replacement = original; replacement.poolHost = "next.pool"; replacement.miningPower = 100;
    for (int fault : {1, 2, 3}) {
        storage.fault = fault;
        assert(!settingsstore::save(storage, replacement, sequence) && sequence == 1);
        Settings read; uint32_t loaded = 0;
        assert(settingsstore::load(storage, read, loaded));
        assert(read.poolHost == "original.pool" && read.miningPower == 75 && loaded == 1);
    }
    storage.fault = 0;
    replacement.webPassword = ""; replacement.batterySaver = true;
    assert(settingsstore::save(storage, replacement, sequence) && sequence == 2);
    Settings read; uint32_t loaded = 0;
    assert(settingsstore::load(storage, read, loaded));
    assert(read.poolHost == "next.pool" && read.miningPower == 100 && read.webPassword.empty() && read.batterySaver);
    storage.bytes["cfgA"][offsetof(settingsstore::Header, sequence)] ^= 1;
    assert(settingsstore::load(storage, read, loaded) && loaded == 1); // corrupt sequence protected by CRC
    Storage wrapped; sequence = UINT32_MAX - 1;
    assert(settingsstore::save(wrapped, original, sequence));
    assert(settingsstore::save(wrapped, replacement, sequence) && sequence == 0);
    assert(settingsstore::load(wrapped, read, loaded) && loaded == 0 && read.poolHost == "next.pool");
    replacement.peers = std::string(9000, 'x'); // not in this descriptor fixture
    replacement.poolHost = std::string(9000, 'x');
    assert(!settingsstore::save(wrapped, replacement, sequence));
    // Older customized identities migrate to manual; old default identities
    // and records without an identity retain automatic mode and V1.
    const char *legacy = R"({"pool":"legacy.pool","power":75,"ua":"custom-old-agent"})";
    Settings migrated;
    assert(settingsstore::decode(reinterpret_cast<const uint8_t *>(legacy), strlen(legacy), migrated));
    assert(migrated.poolHost == "legacy.pool" && migrated.miningPower == 75);
    assert(!migrated.userAgentAuto && migrated.userAgent == "custom-old-agent" && migrated.poolProtocol == 0);
    legacy = R"({"pool":"legacy.pool","ua":"NerdMinerV2"})";
    assert(settingsstore::decode(reinterpret_cast<const uint8_t *>(legacy), strlen(legacy), migrated));
    assert(migrated.userAgentAuto);
    Storage modes;uint32_t modeSequence=0;
    Settings manual;manual.userAgentAuto=false;manual.userAgent="MyMiner";
    manual.poolProtocol=1;manual.poolAuthority="authority-main";manual.backupProtocol=0;manual.backupAuthority="authority-backup";
    assert(settingsstore::save(modes,manual,modeSequence));
    Settings restored;assert(settingsstore::load(modes,restored,modeSequence));
    assert(!restored.userAgentAuto&&restored.userAgent=="MyMiner"&&restored.poolProtocol==1&&restored.backupProtocol==0);
    assert(restored.poolAuthority=="authority-main"&&restored.backupAuthority=="authority-backup");
    puts("Settings records: failed/truncated/corrupt writes, rollback, passwords and generation wrap passed");
}
