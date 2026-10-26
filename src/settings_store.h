// Two complete settings records: an unsuccessful write never replaces the
// last valid configuration. Legacy per-key preferences remain readable.
#pragma once
#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include <vector>
#include "settings.h"

namespace settingsstore {
struct Header { uint32_t magic, sequence, length, crc; };
constexpr uint32_t kMagic = 0x32474643;
constexpr size_t kMaxPayload = 8192;

inline uint32_t checksum(const uint8_t *data, size_t length) {
    uint32_t crc = ~0u;
    while (length--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

inline bool encode(const Settings &values, std::string &text) {
    JsonDocument doc;
    for (size_t i = 0; i < kSettingCount; ++i) {
        const auto &s = kSettings[i];
        const void *value = settingValue(s, values);
        switch (s.type) {
            case SettingType::Text:
            case SettingType::Secret: doc[s.key] = *(const String *)value; break;
            case SettingType::Number:
                doc[s.key] = s.bits == 8 ? *(const uint8_t *)value : *(const uint16_t *)value; break;
            case SettingType::Decimal: doc[s.key] = *(const double *)value; break;
            case SettingType::Bool: doc[s.key] = *(const bool *)value; break;
        }
    }
    if (doc.overflowed()) return false;
    text.clear();
    serializeJson(doc, text);
    return text.size() <= kMaxPayload && !text.empty();
}

inline bool decode(const uint8_t *payload, size_t length, Settings &values) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) || !doc.is<JsonObject>()) return false;
    for (size_t i = 0; i < kSettingCount; ++i) {
        const auto &s = kSettings[i];
        JsonVariantConst v = doc[s.key];
        if (v.isUnbound()) continue;  // settings added by a newer firmware keep their defaults
        void *value = settingValue(s, values);
        switch (s.type) {
            case SettingType::Text:
            case SettingType::Secret:
                if (!v.is<const char *>()) return false;
                *(String *)value = v.as<String>(); break;
            case SettingType::Number:
                if (!v.is<unsigned>() || v.as<unsigned>() > (s.bits == 8 ? 255u : 65535u)) return false;
                if (s.bits == 8) *(uint8_t *)value = v.as<uint8_t>();
                else *(uint16_t *)value = v.as<uint16_t>();
                break;
            case SettingType::Decimal:
                if (!v.is<double>()) return false;
                *(double *)value = v.as<double>(); break;
            case SettingType::Bool:
                if (!v.is<bool>()) return false;
                *(bool *)value = v.as<bool>(); break;
        }
    }
    if (doc["uaauto"].isUnbound() && doc["ua"].is<const char *>())
        values.userAgentAuto = !legacyManualAgent(doc["ua"].as<const char *>());
    return true;
}

template<class Storage>
bool read(Storage &p, const char *key, Settings &values, uint32_t &sequence) {
    size_t size = p.getBytesLength(key);
    if (size < sizeof(Header) || size > sizeof(Header) + kMaxPayload) return false;
    std::vector<uint8_t> bytes(size);
    if (p.getBytes(key, bytes.data(), size) != size) return false;
    Header header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != kMagic || header.length != size - sizeof(header)) return false;
    std::memset(bytes.data() + offsetof(Header, crc), 0, sizeof(header.crc));
    if (checksum(bytes.data(), size) != header.crc) return false;
    if (!decode(bytes.data() + sizeof(Header), header.length, values)) return false;
    sequence = header.sequence;
    return true;
}

template<class Storage>
bool load(Storage &p, Settings &values, uint32_t &sequence) {
    Settings a = values, b = values;
    uint32_t sa = 0, sb = 0;
    bool validA = read(p, "cfgA", a, sa), validB = read(p, "cfgB", b, sb);
    if (!validA && !validB) return false;
    bool pickA = validA && (!validB || int32_t(sa - sb) > 0);
    values = pickA ? a : b;
    sequence = pickA ? sa : sb;
    return true;
}

template<class Storage>
bool save(Storage &p, const Settings &values, uint32_t &sequence) {
    std::string payload;
    if (!encode(values, payload)) return false;
    const size_t length = payload.size();
    // Bound the addition and copy at the point of use too. This keeps the
    // record builder safe independently of encode() and visible to FORTIFY.
    if (!length || length > kMaxPayload) return false;
    Header header{kMagic, sequence + 1, uint32_t(length), 0};
    std::vector<uint8_t> bytes(sizeof(Header) + length);
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), payload.data(), length);
    header.crc = checksum(bytes.data(), bytes.size());
    std::memcpy(bytes.data(), &header, sizeof(header));
    const char *key = header.sequence & 1 ? "cfgB" : "cfgA";
    if (p.putBytes(key, bytes.data(), bytes.size()) != bytes.size()) return false;
    Settings verified = values;
    uint32_t stored = 0;
    if (!read(p, key, verified, stored) || stored != header.sequence) return false;
    sequence = stored;
    return true;
}
}  // namespace settingsstore
