// Bitcoin address checks (Base58Check and Bech32 / Bech32m), used to warn
// about a mistyped payout address instead of mining for nothing.
// Portable (std C++ only) so it can be tested on a PC.
#pragma once

#include <string>

namespace address {

enum class Type { Invalid, P2PKH, P2SH, P2WPKH, P2WSH, P2TR, SegwitOther };
enum class Network { Unknown, Main, Test, Regtest };

struct Info {
    Type type;
    Network network;
    Info(Type t = Type::Invalid, Network n = Network::Unknown) : type(t), network(n) {}
    bool valid() const { return type != Type::Invalid; }
};

// `user` is the Stratum user name: an address, optionally followed by
// ".workerName". Only the address part is checked.
Info parse(const std::string &user);

// "P2WPKH", "P2TR"... or "invalid".
const char *typeName(Type type);

}  // namespace address
