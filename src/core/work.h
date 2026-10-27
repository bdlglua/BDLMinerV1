// Stratum job -> 80-byte block header, plus difficulty helpers.
// Portable (std C++ only) so it can be tested on a PC.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "sha256.h"

namespace work {

// A mining.notify job as received from the pool, already decoded.
struct Job {
    std::string id;
    uint8_t prevHash[32] = {0};  // header byte order
    std::vector<uint8_t> coinb1;
    std::vector<uint8_t> coinb2;
    std::vector<std::vector<uint8_t>> branches;  // 32 bytes each
    uint32_t version = 0;
    uint32_t versionMask = 0; // explicitly negotiated BIP310/BIP320 bits
    uint32_t nbits = 0;
    uint32_t ntime = 0;
    bool clean = false;

    // Session data (from mining.subscribe / mining.set_difficulty).
    std::vector<uint8_t> extranonce1;
    int extranonce2Size = 4;
    double difficulty = 1.0;
    bool exactShareTarget = false; // SV2 supplies a full U256 target
    uint8_t shareTarget[32] = {};

    uint32_t blockHeight() const;  // BIP34 height parsed from coinb1, 0 if unknown
};

// Ready-to-hash work unit derived from a Job and one extranonce2 value.
struct Work {
    std::string jobId;
    std::string extranonce2Hex;
    uint32_t ntime = 0;
    uint32_t versionMask = 0;
    uint8_t header[80];
    sha::MidState mid;
    double shareDifficulty = 1.0;
    uint32_t quickLimit = 0;  // top-32-bit filter passed to sha::scan
    uint8_t networkTarget[32];  // little-endian
    bool exactShareTarget = false;
    uint8_t shareTarget[32] = {};
};

void build(const Job &job, uint64_t extranonce2, Work &out);

// Difficulty of a hash (32 bytes, as produced by sha256d) relative to the
// difficulty-1 target 0x00000000FFFF0000...
double hashDifficulty(const uint8_t hash[32]);

// Top 32 bits of the target matching `difficulty` (little-endian view).
uint32_t quickLimitFor(double difficulty);

// nBits -> 256-bit little-endian target.
void targetFromBits(uint32_t nbits, uint8_t target[32]);
double difficultyFromBits(uint32_t nbits);

// true if little-endian 256-bit `hash` <= `target`.
bool meetsTarget(const uint8_t hash[32], const uint8_t target[32]);

// Hex helpers.
bool fromHex(const char *hex, std::vector<uint8_t> &out);
bool fromHex(const char *hex, uint8_t *out, size_t len);
std::string toHex(const uint8_t *data, size_t len);

// Human readable number: 12.3k, 4.56M ...
std::string formatSI(double v, int decimals = 2);

}  // namespace work
