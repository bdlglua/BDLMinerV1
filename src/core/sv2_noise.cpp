#include "sv2_noise.h"
#include "sha256.h"
#include "work.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <monocypher.h>
#include <secp256k1.h>
#include <secp256k1_preallocated.h>
#include <secp256k1_ellswift.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_schnorrsig.h>

namespace sv2 {
namespace {
// All handshake hashes use the portable software kernel: they cannot contend
// with the miner's hardware SHA peripheral or acquire the TLS engine locks.
void mix(uint8_t hash[32], const uint8_t *data, size_t length) {
    std::vector<uint8_t> bytes(hash, hash + 32);
    if (length) bytes.insert(bytes.end(), data, data + length);
    sha::sha256(bytes.data(), bytes.size(), hash);
}
void hmac(const uint8_t key[32], const uint8_t *data, size_t length, uint8_t out[32]) {
    uint8_t buf[128]{};
    for (int i = 0; i < 64; ++i) buf[i] = (i < 32 ? key[i] : 0) ^ 0x36;
    if (length) memcpy(buf + 64, data, length); // HKDF inputs are at most 33 bytes
    uint8_t inner[32]; sha::sha256(buf, 64 + length, inner);
    for (int i = 0; i < 64; ++i) buf[i] = (i < 32 ? key[i] : 0) ^ 0x5c;
    memcpy(buf + 64, inner, 32); sha::sha256(buf, 96, out);
    crypto_wipe(buf, sizeof(buf)); crypto_wipe(inner, sizeof(inner));
}
void hkdf(const uint8_t ck[32], const uint8_t *input, size_t length, uint8_t one[32], uint8_t two[32]) {
    uint8_t prk[32], message[33]{}; hmac(ck, input, length, prk);
    message[0] = 1; hmac(prk, message, 1, one);
    memcpy(message, one, 32); message[32] = 2; hmac(prk, message, 33, two);
    crypto_wipe(prk, sizeof(prk)); crypto_wipe(message, sizeof(message));
}
void nonce(uint64_t counter, uint8_t out[12]) {
    memset(out, 0, 12); for (int i = 0; i < 8; ++i) out[4 + i] = counter >> (8 * i);
}
bool decryptWith(const uint8_t key[32], uint64_t counter, const uint8_t *ad, size_t adSize,
                 const uint8_t *in, size_t length, uint8_t *out) {
    uint8_t n[12]; nonce(counter, n);
    crypto_aead_ctx ctx; crypto_aead_init_ietf(&ctx, key, n);
    bool ok = crypto_aead_read(&ctx, out, in + length - 16, ad, adSize, in, length - 16) == 0;
    crypto_wipe(&ctx, sizeof(ctx)); return ok;
}
uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
}


Noise::~Noise() {
    if (context_) secp256k1_context_preallocated_destroy(static_cast<secp256k1_context *>(context_));
    if (arena_) { crypto_wipe(arena_, secp256k1_context_preallocated_size(SECP256K1_CONTEXT_NONE)); free(arena_); }
    crypto_wipe(secret_, sizeof(secret_)); crypto_wipe(ck_, sizeof(ck_));
    crypto_wipe(tx_, sizeof(tx_)); crypto_wipe(rx_, sizeof(rx_));
}
bool Noise::fail(const char *message) {
    ready_ = waiting_ = false; error_ = message;
    crypto_wipe(secret_, sizeof(secret_)); crypto_wipe(ck_, sizeof(ck_));
    crypto_wipe(tx_, sizeof(tx_)); crypto_wipe(rx_, sizeof(rx_)); return false;
}
bool Noise::start(const uint8_t authority[32], Random random, uint8_t hello[64]) {
    if (!authority || !random) return fail("SV2 authority key required");
    if (context_) secp256k1_context_preallocated_destroy(static_cast<secp256k1_context *>(context_));
    if (arena_) { crypto_wipe(arena_, secp256k1_context_preallocated_size(SECP256K1_CONTEXT_NONE)); free(arena_); }
    context_ = nullptr;
    arena_ = malloc(secp256k1_context_preallocated_size(SECP256K1_CONTEXT_NONE));
    if (!arena_) return fail("SV2 crypto allocation failed");
    auto *ctx = secp256k1_context_preallocated_create(arena_, SECP256K1_CONTEXT_NONE); context_ = ctx;
    if (!ctx) return fail("SV2 crypto allocation failed");
    ready_ = waiting_ = false; txNonce_ = rxNonce_ = 0;
    secp256k1_xonly_pubkey auth;
    if (!secp256k1_xonly_pubkey_parse(ctx, &auth, authority)) return fail("Invalid SV2 authority key");
    memcpy(authority_, authority, 32);
    uint8_t seed[32]; random(seed, 32);
    bool randomized = secp256k1_context_randomize(ctx, seed); crypto_wipe(seed, 32);
    if (!randomized) return fail("SV2 crypto randomization failed");
    bool valid = false;
    for (int i = 0; i < 8 && !valid; ++i) { random(secret_, 32); valid = secp256k1_ec_seckey_verify(ctx, secret_); }
    if (!valid) return fail("SV2 random key generation failed");
    random(seed, 32);
    bool generated = secp256k1_ellswift_create(ctx, ephemeral_, secret_, seed); crypto_wipe(seed, 32);
    if (!generated) return fail("SV2 ephemeral key failed");
    const char name[] = "Noise_NX_Secp256k1+EllSwift_ChaChaPoly_SHA256";
    sha::sha256(reinterpret_cast<const uint8_t *>(name), sizeof(name) - 1, h_);
    memcpy(ck_, h_, 32); mix(h_, nullptr, 0); mix(h_, ephemeral_, 64); mix(h_, nullptr, 0);
    memcpy(hello, ephemeral_, 64); waiting_ = true; error_ = "Waiting for SV2 Noise response"; return true;
}
bool Noise::finish(const uint8_t response[234], uint32_t now) {
    if (!context_ || !waiting_ || ready_) return fail("SV2 handshake not started");
    if (now < 1700000000u) return fail("SV2 requires synchronized time");
    auto *ctx = static_cast<secp256k1_context *>(context_);
    uint8_t shared[32], key[32], server[64], certificate[74];
    mix(h_, response, 64);
    if (!secp256k1_ellswift_xdh(ctx, shared, ephemeral_, response, secret_, 0,
                              secp256k1_ellswift_xdh_hash_function_bip324, nullptr)) return fail("SV2 ECDH failed");
    hkdf(ck_, shared, 32, ck_, key); crypto_wipe(shared, 32);
    bool decrypted = decryptWith(key, 0, h_, 32, response + 64, 80, server); crypto_wipe(key, 32);
    if (!decrypted) return fail("SV2 server key authentication failed");
    mix(h_, response + 64, 80);
    if (!secp256k1_ellswift_xdh(ctx, shared, ephemeral_, server, secret_, 0,
                              secp256k1_ellswift_xdh_hash_function_bip324, nullptr)) return fail("SV2 ECDH failed");
    hkdf(ck_, shared, 32, ck_, key); crypto_wipe(shared, 32);
    decrypted = decryptWith(key, 0, h_, 32, response + 144, 90, certificate); crypto_wipe(key, 32);
    if (!decrypted) return fail("SV2 certificate authentication failed");
    if (certificate[0] || certificate[1]) return fail("Unsupported SV2 certificate version");
    uint32_t start = le32(certificate + 2), end = le32(certificate + 6);
    if (start > end || now < start || now > end) return fail("SV2 certificate outside validity period");
    uint8_t signedData[42], hash[32]; memcpy(signedData, certificate, 10);
    secp256k1_pubkey point; secp256k1_xonly_pubkey xonly, auth; int parity;
    if (!secp256k1_ellswift_decode(ctx, &point, server) ||
        !secp256k1_xonly_pubkey_from_pubkey(ctx, &xonly, &parity, &point) ||
        !secp256k1_xonly_pubkey_serialize(ctx, signedData + 10, &xonly) ||
        !secp256k1_xonly_pubkey_parse(ctx, &auth, authority_)) return fail("Invalid SV2 certificate key");
    sha::sha256(signedData, sizeof(signedData), hash);
    if (!secp256k1_schnorrsig_verify(ctx, certificate + 10, hash, 32, &auth)) return fail("SV2 authority signature mismatch");
    hkdf(ck_, nullptr, 0, tx_, rx_);
    crypto_wipe(secret_, 32); crypto_wipe(ck_, 32); crypto_wipe(h_, 32);
    ready_ = true; waiting_ = false; error_ = ""; return true;
}
bool Noise::encrypt(const uint8_t *plain, size_t length, uint8_t *cipher) {
    if (!ready_ || length > 65519 || txNonce_ == UINT64_MAX) return fail("Invalid SV2 encrypted record");
    uint8_t n[12]; nonce(txNonce_, n);
    crypto_aead_ctx ctx; crypto_aead_init_ietf(&ctx, tx_, n);
    crypto_aead_write(&ctx, cipher, cipher + length, nullptr, 0, plain, length);
    crypto_wipe(&ctx, sizeof(ctx));
    ++txNonce_; return true;
}
bool Noise::decrypt(const uint8_t *cipher, size_t length, uint8_t *plain) {
    if (!ready_ || length < 16 || length > 65535 || rxNonce_ == UINT64_MAX) return fail("Invalid SV2 encrypted record");
    if (!decryptWith(rx_, rxNonce_, nullptr, 0, cipher, length, plain)) return fail("SV2 message authentication failed");
    ++rxNonce_; return true;
}
} // namespace sv2
