// Expose trusted libsecp256k1 primitives to the independent responder fixture.
#include <secp256k1.h>
#include <secp256k1_ellswift.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_schnorrsig.h>
static secp256k1_context *context;
static secp256k1_context *ctx(void) { if (!context) context = secp256k1_context_create(SECP256K1_CONTEXT_NONE); return context; }
int curve_ell(const unsigned char *secret, unsigned char *out) { unsigned char aux[32] = {0}; return secp256k1_ellswift_create(ctx(), out, secret, aux); }
int curve_dh(const unsigned char *secret, const unsigned char *a, const unsigned char *b, int party, unsigned char *out) {
    return secp256k1_ellswift_xdh(ctx(), out, a, b, secret, party, secp256k1_ellswift_xdh_hash_function_bip324, 0);
}
int curve_xonly(const unsigned char *secret, unsigned char *out) {
    secp256k1_keypair pair; secp256k1_xonly_pubkey pub;
    if (!secp256k1_keypair_create(ctx(), &pair, secret) || !secp256k1_keypair_xonly_pub(ctx(), &pub, 0, &pair)) return 0;
    return secp256k1_xonly_pubkey_serialize(ctx(), out, &pub);
}
int curve_sign(const unsigned char *secret, const unsigned char *hash, unsigned char *out) {
    secp256k1_keypair pair; unsigned char aux[32] = {0};
    if (!secp256k1_keypair_create(ctx(), &pair, secret)) return 0;
    return secp256k1_schnorrsig_sign32(ctx(), out, hash, &pair, aux);
}
