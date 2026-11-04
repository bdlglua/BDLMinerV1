#include "address.h"

#include <ctype.h>
#include <string.h>

#include <vector>

#include "sha256.h"
#include "coin.h"

namespace address {

namespace {

const char kBase58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
const char kBech32[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
constexpr uint32_t kBech32Const = 1;
constexpr uint32_t kBech32mConst = 0x2bc830a3;

// Legacy address: 25 bytes = version + hash160 + 4-byte sha256d checksum.
bool parseBase58(const std::string &s, Info &info, unsigned selectedCoin = coin::BTC) {
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

    if (selectedCoin == coin::DGB) {
        switch (num[0]) {
            case 30: info = Info(Type::P2PKH, Network::Main); return true;
            case 5: case 63: info = Info(Type::P2SH, Network::Main); return true;
            case 126: info = Info(Type::P2PKH, Network::Test); return true;
            case 140: info = Info(Type::P2SH, Network::Test); return true;
            default: return false;
        }
    }
    if (selectedCoin == coin::PPC) {
        switch (num[0]) {
            case 55: info = Info(Type::P2PKH, Network::Main); return true;
            case 117: info = Info(Type::P2SH, Network::Main); return true;
            case 111: info = Info(Type::P2PKH, Network::Test); return true;
            case 196: info = Info(Type::P2SH, Network::Test); return true;
            default: return false;
        }
    }
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
bool parseSegwit(const std::string &addr, Info &info, unsigned selectedCoin = coin::BTC) {
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
    const bool dgb = selectedCoin == coin::DGB;
    const bool ppc = selectedCoin == coin::PPC;
    Network net = hrp == (dgb ? "dgb" : ppc ? "pc" : "bc") ? Network::Main :
                  (hrp == (dgb ? "dgbt" : ppc ? "tpc" : "tb") || (ppc && hrp == "tb")) ? Network::Test :
                  hrp == (dgb ? "dgbrt" : ppc ? "pcrt" : "bcrt") ? Network::Regtest : Network::Unknown;
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



}  // namespace

// CashAddr specification: https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/cashaddr.md
// Bounded decoding verifies the prefix, 40-bit checksum, version, hash length
// and zero padding. It never corrects a mistyped payout address.
static bool parseCashAddr(const std::string &addr, Info &info, unsigned selectedCoin) {
    if (addr.empty() || addr.size() > 124) return false;
    bool lower = false, upper = false;
    std::string text;
    for (unsigned char c : addr) {
        if (c < 33 || c > 126) return false;
        lower |= std::islower(c); upper |= std::isupper(c);
        text += char(std::tolower(c));
    }
    if (lower && upper) return false;
    size_t colon = text.find(':');
    std::string prefix = colon == std::string::npos ? coin::cashPrefix(selectedCoin) : text.substr(0, colon);
    Network network = prefix == coin::cashPrefix(selectedCoin, 0) ? Network::Main :
                      prefix == coin::cashPrefix(selectedCoin, 1) ? Network::Test :
                      prefix == coin::cashPrefix(selectedCoin, 2) ? Network::Regtest : Network::Unknown;
    if (network == Network::Unknown) return false;
    std::string payload = colon == std::string::npos ? text : text.substr(colon + 1);
    if (payload.size() < 42 || payload.size() > 112) return false;
    uint64_t check = 1;
    auto step = [&](uint8_t value) {
        static const uint64_t generator[] = {0x98f2bc8e61ULL, 0x79b76d99e2ULL, 0xf33e5fb3c4ULL, 0xae2eabe2a8ULL, 0x1e4f43e470ULL};
        uint8_t top = check >> 35;
        check = ((check & 0x07ffffffffULL) << 5) ^ value;
        for (unsigned i = 0; i < 5; ++i) if (top & (1 << i)) check ^= generator[i];
    };
    for (char c : prefix) step(c & 31);
    step(0);
    uint8_t decoded[65]{};
    unsigned length = 0, bits = 0;
    uint32_t accumulator = 0;
    for (size_t i = 0; i < payload.size(); ++i) {
        const char *p = strchr(kBech32, payload[i]);
        if (!p) return false;
        unsigned value = unsigned(p - kBech32);
        step(value);
        if (i + 8 >= payload.size()) continue;
        accumulator = ((accumulator << 5) | value) & 0xfff;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (length == sizeof(decoded)) return false;
            decoded[length++] = uint8_t(accumulator >> bits);
        }
    }
    if (check != 1 || bits >= 5 || ((accumulator << (8 - bits)) & 255) || !length) return false;
    static const unsigned sizes[] = {20, 24, 28, 32, 40, 48, 56, 64};
    uint8_t version = decoded[0];
    if ((version & 0x80) || length != sizes[version & 7] + 1 || (version >> 3) > 1) return false;
    info = Info((version >> 3) ? Type::P2SH : Type::P2PKH, network);
    return true;
}

Info parse(const std::string &user, unsigned selectedCoin) {
    std::string addr = user.substr(0, user.find('.'));
    Info info;
    if (!coin::valid(selectedCoin)) return info;
    if (coin::cashPrefix(selectedCoin) && parseCashAddr(addr, info, selectedCoin)) return info;
    bool segwit = selectedCoin == coin::BTC || selectedCoin == coin::DGB || selectedCoin == coin::PPC;
    if (!(segwit && parseSegwit(addr, info, selectedCoin)) && !parseBase58(addr, info, selectedCoin)) info = Info();
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
