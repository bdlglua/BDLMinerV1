#include "address.h"

#include <ctype.h>
#include <string.h>

#include <vector>

#include "sha256.h"

namespace address {

namespace {

const char kBase58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
const char kBech32[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
constexpr uint32_t kBech32Const = 1;
constexpr uint32_t kBech32mConst = 0x2bc830a3;

// Legacy address: 25 bytes = version + hash160 + 4-byte sha256d checksum.
bool parseBase58(const std::string &s, Info &info) {
    if (s.size() < 26 || s.size() > 35) return false;
    uint8_t num[25] = {0};
    for (char c : s) {
        const char *p = c ? strchr(kBase58, c) : nullptr;
        if (!p) return false;
        uint32_t carry = (uint32_t)(p - kBase58);
        for (int i = 24; i >= 0; i--) {
            carry += 58u * num[i];
            num[i] = carry & 0xFF;
            carry >>= 8;
        }
        if (carry) return false;  // longer than 25 bytes
    }
    // Each leading '1' encodes one leading zero byte, and only those do.
    size_t ones = 0, zeros = 0;
    while (ones < s.size() && s[ones] == '1') ones++;
    while (zeros < sizeof(num) && num[zeros] == 0) zeros++;
    if (ones != zeros) return false;

    uint8_t hash[32];
    sha::sha256d(num, 21, hash);
    if (memcmp(hash, num + 21, 4) != 0) return false;

    switch (num[0]) {
        case 0x00: info = Info(Type::P2PKH, Network::Main); return true;
        case 0x05: info = Info(Type::P2SH, Network::Main); return true;
        case 0x6F: info = Info(Type::P2PKH, Network::Test); return true;
        case 0xC4: info = Info(Type::P2SH, Network::Test); return true;
        default: return false;
    }
}

uint32_t polymod(const std::vector<uint8_t> &values) {
    static const uint32_t kGen[5] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};
    uint32_t chk = 1;
    for (uint8_t v : values) {
        uint8_t top = chk >> 25;
        chk = ((chk & 0x1FFFFFF) << 5) ^ v;
        for (int i = 0; i < 5; i++)
            if ((top >> i) & 1) chk ^= kGen[i];
    }
    return chk;
}

// SegWit address (BIP173 / BIP350): hrp "1" witness version + program + checksum.
bool parseSegwit(const std::string &addr, Info &info) {
    if (addr.size() > 90) return false;
    bool lower = false, upper = false;
    std::string s;
    for (char c : addr) {
        if (c < 33 || c > 126) return false;
        if (islower((unsigned char)c)) lower = true;
        if (isupper((unsigned char)c)) upper = true;
        s += (char)tolower((unsigned char)c);
    }
    if (lower && upper) return false;

    size_t sep = s.rfind('1');
    if (sep == std::string::npos || sep == 0 || sep + 8 > s.size()) return false;
    std::string hrp = s.substr(0, sep);
    Network net = hrp == "bc" ? Network::Main : hrp == "tb" ? Network::Test : hrp == "bcrt" ? Network::Regtest
                                                                                          : Network::Unknown;
    if (net == Network::Unknown) return false;

    std::vector<uint8_t> data;
    for (size_t i = sep + 1; i < s.size(); i++) {
        const char *p = strchr(kBech32, s[i]);
        if (!p) return false;
        data.push_back((uint8_t)(p - kBech32));
    }
    std::vector<uint8_t> values;
    for (char c : hrp) values.push_back((uint8_t)c >> 5);
    values.push_back(0);
    for (char c : hrp) values.push_back((uint8_t)c & 31);
    values.insert(values.end(), data.begin(), data.end());

    uint8_t version = data[0];
    if (version > 16) return false;
    if (polymod(values) != (version == 0 ? kBech32Const : kBech32mConst)) return false;

    // Witness program: 5-bit groups (minus version and checksum) -> bytes.
    std::vector<uint8_t> program;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 1; i + 6 < data.size(); i++) {
        acc = ((acc << 5) | data[i]) & 0xFFF;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            program.push_back((acc >> bits) & 0xFF);
        }
    }
    if (bits >= 5 || (acc & ((1u << bits) - 1))) return false;  // non-zero padding
    if (program.size() < 2 || program.size() > 40) return false;

    Type type = Type::SegwitOther;
    if (version == 0) {
        if (program.size() == 20) type = Type::P2WPKH;
        else if (program.size() == 32) type = Type::P2WSH;
        else return false;
    } else if (version == 1 && program.size() == 32) {
        type = Type::P2TR;
    }
    info = Info(type, net);
    return true;
}

bool startsWithNoCase(const std::string &s, const char *prefix) {
    for (size_t i = 0; prefix[i]; i++)
        if (i >= s.size() || tolower((unsigned char)s[i]) != prefix[i]) return false;
    return true;
}

}  // namespace

Info parse(const std::string &user) {
    std::string addr = user.substr(0, user.find('.'));
    Info info;
    bool segwit = startsWithNoCase(addr, "bc1") || startsWithNoCase(addr, "tb1") || startsWithNoCase(addr, "bcrt1");
    if (!(segwit ? parseSegwit(addr, info) : parseBase58(addr, info))) info = Info();
    return info;
}

const char *typeName(Type type) {
    switch (type) {
        case Type::P2PKH: return "P2PKH";
        case Type::P2SH: return "P2SH";
        case Type::P2WPKH: return "P2WPKH";
        case Type::P2WSH: return "P2WSH";
        case Type::P2TR: return "P2TR";
        case Type::SegwitOther: return "SegWit";
        default: return "invalid";
    }
}

}  // namespace address
