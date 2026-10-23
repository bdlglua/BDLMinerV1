// Hardware SHA-256 mining engine.
//  - ESP32-S2/S3/C3: the peripheral resumes from a saved midstate, so one
//    nonce costs two blocks.
//  - Original ESP32 (revision 3 and later): the peripheral cannot be given a
//    midstate, so one nonce costs three blocks, but a cycle-counted assembly
//    loop keeps it busy all the time (hw_sha_classic.S, ~1 MH/s at 240 MHz).
#pragma once

#include <stdint.h>

#include "core/sha256.h"

namespace hwsha {

struct Job {
    uint32_t mid[8];      // resumable engines: midstate in register format
    uint32_t tail[16];    // resumable engines: second header block, nonce in tail[3]
    uint32_t words[20];   // original ESP32: the header as big-endian engine words
    sha::MidState check;  // original ESP32: software midstate to confirm candidates
};

bool available();

void prepare(Job &job, const uint8_t header[80]);

// Engines scan nonces in their own order: position p is nonceAt(p). Over the
// 2^32 positions every nonce comes up exactly once.
uint32_t nonceAt(uint32_t position);

// Scans positions [first, first + count). For every nonce whose hash top-32
// bits (little-endian view) are <= limit, calls hit(nonce, hash, ctx). On the
// original ESP32 only hashes with top-32 bits <= 0xFFFF are seen, whatever
// `limit` is. Returns the number of hashes done, 0 when the peripheral is in
// use elsewhere (TLS) or has been disabled: the caller then hashes in software.
uint32_t scan(Job &job, uint32_t first, uint32_t count, uint32_t limit,
              void (*hit)(uint32_t nonce, const uint8_t hash[32], void *ctx), void *ctx);

// Keeps the mining loop off the peripheral until release(), waiting for the
// batch in progress to end. On the original ESP32 the loop saturates the DPORT
// bus, which slows the RSA accelerator down so much that a TLS handshake on
// the other core starves its idle task (task watchdog reset): HTTPS requests
// run inside hold()/release(). The miner hashes in software meanwhile.
// Called from one task at a time; a no-op on the other chips.
void hold();
void release();

// Checks the peripheral against known answers. The hardware path is only
// used if this passes.
bool selfTest();

}  // namespace hwsha
