// Portable SHA-256 helpers and the software mining kernel.
// No Arduino dependency so it can be unit-tested on a PC (see test/).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sha {

// Plain SHA-256 / double SHA-256 (used for coinbase and merkle tree).
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void sha256d(const uint8_t *data, size_t len, uint8_t out[32]);

// Compress one 64-byte block into state (standard big-endian words).
void transform(uint32_t state[8], const uint8_t block[64]);

// Everything that is constant for a given 80-byte block header, precomputed
// once per job so the per-nonce cost is as low as possible.
struct MidState {
    uint32_t mid[8];   // state after the first 64 header bytes
    uint32_t pre[8];   // `mid` advanced through rounds 0..2 of the 2nd block
    uint32_t w[18];    // second block message words 0..17 (w[3] = nonce)
};

void prepare(MidState &ms, const uint8_t header[80]);

// Hash header(nonce) twice. Returns false early (without filling `out`) when
// the top 32 bits of the hash, read as a little-endian 256-bit number, are
// above `limit`. Otherwise writes the 32-byte hash and returns true.
bool scan(const MidState &ms, uint32_t nonce, uint32_t limit, uint8_t out[32]);

}  // namespace sha
