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

// Original ESP32: a software SHA-256d of a second work that the hardware loop
// runs in the cycles it otherwise waits for the engine (hw_sha_fill.S), one
// nonce per 64 hardware nonces, without slowing the hardware down. Nonces
// come in the same order as the engine's (positions). Only one Fill can be
// in use (its buffers are static).
struct Fill {
    sha::MidState check;  // to confirm its candidates
    uint32_t p = 0, q = 0;  // state of the generated code between scans
    uint32_t hashes = 0;    // nonces done; the caller adds them up and resets it
    void *ctx = nullptr;    // passed to hit() for its candidates
    bool ready = false;
};

// True when the self-test found the idle-cycle hashing correct and faster.
bool fillAvailable();
// Starts hashing `header` (nonce bytes ignored) from position 0.
void prepareFill(Fill &fill, const uint8_t header[80]);

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
// With `fill`, its work is hashed too when available; its candidates go to
// hit() with fill->ctx.
uint32_t scan(Job &job, uint32_t first, uint32_t count, uint32_t limit,
              void (*hit)(uint32_t nonce, const uint8_t hash[32], void *ctx), void *ctx, Fill *fill = nullptr);

// TLS handshakes run inside hold()/release() (they nest). S2/S3/C3: keeps
// the mining loop off the peripheral (waiting for the batch in progress; the
// miner hashes in software meanwhile), as TLS would otherwise wait for a
// whole batch at every SHA or AES operation. Original ESP32: the miner keeps
// the SHA engines between batches, so TLS hashes in software and mining goes
// on (see hw_sha.cpp). Called from one task at a time.
void hold();
void release();

// Called by the mining task when it stops scanning for a while (paused, no
// work): frees the engines scan() may still hold.
void idle();

// Checks the peripheral against known answers. The hardware path is only
// used if this passes.
bool selfTest();

}  // namespace hwsha
