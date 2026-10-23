#include "fill.h"

#include <string.h>

#include "sha256.h"

namespace fill {

namespace {

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t S0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t S1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t s1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return z ^ (x & (y ^ z)); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (z & (x | y)); }

enum Field { A, E, W, KW, K };

uint32_t &b1(Buffers &buf, int round, Field f) { return buf.b1[round * kRecordWords + f]; }
uint32_t &b2(Buffers &buf, int round, Field f) { return buf.b2[(round + 3) * kRecordWords + f]; }

}  // namespace

void prepare(Buffers &buf, const uint8_t header[80]) {
    memset(&buf, 0, sizeof(buf));
    sha::MidState ms;
    sha::prepare(ms, header);
    memcpy(buf.mid, ms.mid, sizeof(buf.mid));

    for (int i = 0; i < kB1Records; i++) b1(buf, i, K) = i < 64 ? sha::K[i] : 0;
    for (int i = -3; i < kB2Records - 3; i++) b2(buf, i, K) = i >= 0 && i < 64 ? sha::K[i] : 0;

    // First hash: message words 0..17, W[3] (the nonce) and W[18] (which
    // depends on it) are written per nonce by the generated code.
    uint32_t w[18] = {ms.w[0], ms.w[1], ms.w[2], 0, 0x80000000};
    w[15] = 640;
    w[16] = ms.w[16];
    w[17] = ms.w[17];
    for (int i = 0; i < 18; i++) {
        b1(buf, i, W) = w[i];
        if (i >= 4) b1(buf, i, KW) = sha::K[i] + w[i];
    }
    buf.c18 = s1(w[16]) + w[11] + w[2];

    // `a` and `e` before rounds 0..3: rounds 0..2 do not depend on the nonce.
    uint32_t a = ms.mid[0], b = ms.mid[1], c = ms.mid[2], d = ms.mid[3];
    uint32_t e = ms.mid[4], f = ms.mid[5], g = ms.mid[6], h = ms.mid[7];
    for (int i = 0; i <= 3; i++) {
        b1(buf, i, A) = a;
        b1(buf, i, E) = e;
        if (i == 3) break;
        uint32_t t1 = h + S1(e) + ch(e, f, g) + sha::K[i] + w[i];
        uint32_t t2 = S0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    // Second hash: starts from the initial state, words 8..15 are padding.
    for (int i = 0; i < 4; i++) {
        b2(buf, -i, A) = sha::IV[i];
        b2(buf, -i, E) = sha::IV[4 + i];
    }
    for (int i = 8; i < 16; i++) {
        uint32_t v = i == 8 ? 0x80000000 : i == 15 ? 256 : 0;
        b2(buf, i, W) = v;
        b2(buf, i, KW) = sha::K[i] + v;
    }
    // No second hash done yet: `e` after round 60 + IV[7] must not end in 16 zero bits.
    b2(buf, 61, E) = 1 - sha::IV[7];
}

}  // namespace fill
