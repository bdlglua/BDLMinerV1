// Explicit Stratum identities: the native option follows the firmware version.
#pragma once
#include <cstring>
#include <string>

namespace user_agent {
constexpr const char *kNerdMiner = "NerdMinerV2";
inline const char *normalize(const char *saved, const char *native) {
    return saved && !std::strcmp(saved, kNerdMiner) ? kNerdMiner : native;
}
inline bool valid(const char *value, const char *native) {
    return value && (!std::strcmp(value, kNerdMiner) || !std::strcmp(value, native));
}
inline std::string portalSelect(const char *saved, const char *native) {
    const bool nerd = saved && !std::strcmp(saved, kNerdMiner);
    return std::string("<label for='ua'>User agent</label><select id='ua' name='ua'>") +
        "<option value='NerdMinerV2'" + (nerd ? " selected" : "") + ">NerdMinerV2</option>" +
        "<option value='" + native + "'" + (nerd ? "" : " selected") + ">" + native + "</option></select>";
}
} // namespace user_agent
