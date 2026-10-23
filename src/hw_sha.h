// Hardware SHA-256 mining engine for chips whose SHA peripheral can resume
// from a saved state (ESP32-S2, ESP32-S3, ESP32-C3). On the original ESP32
// this engine is unavailable and the software kernel is used instead.
#pragma once

#include <stdint.h>

namespace hwsha {

struct Job {
    uint32_t mid[8];    // midstate in engine register format
    uint32_t tail[16];  // second header block (+ padding), nonce in tail[3]
};

bool available();

// Compute the midstate with the peripheral.
void prepare(Job &job, const uint8_t header[80]);

// Scans [first, first + count). For every nonce whose hash top-32 bits
// (little-endian view) are <= limit, calls hit(nonce, hash, ctx).
// Returns the number of hashes done.
uint32_t scan(Job &job, uint32_t first, uint32_t count, uint32_t limit,
              void (*hit)(uint32_t nonce, const uint8_t hash[32], void *ctx), void *ctx);

// Hashes the genesis block with the peripheral and compares with the known
// result. The hardware path is only used if this passes.
bool selfTest();

}  // namespace hwsha
