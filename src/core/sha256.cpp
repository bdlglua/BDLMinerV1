#include "sha256.h"

#include <string.h>

namespace sha {

namespace {

const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

const uint32_t IV[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t S0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t S1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t s0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline uint32_t s1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return z ^ (x & (y ^ z)); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (z & (x | y)); }

inline uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

inline void put_be32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

inline uint32_t bswap(uint32_t v) { return __builtin_bswap32(v); }

// One SHA-256 round. s[] holds a..h; the rotation is done by the caller
// through index arithmetic so the compiler keeps everything in registers.
#define ROUND(a, b, c, d, e, f, g, h, k, w)                 \
    do {                                                    \
        uint32_t t1 = h + S1(e) + ch(e, f, g) + (k) + (w);  \
        uint32_t t2 = S0(a) + maj(a, b, c);                 \
        d += t1;                                            \
        h = t1 + t2;                                        \
    } while (0)

#define ROUND8(i, W)                                                    \
    ROUND(a, b, c, d, e, f, g, h, K[(i) + 0], W[(i) + 0]);              \
    ROUND(h, a, b, c, d, e, f, g, K[(i) + 1], W[(i) + 1]);              \
    ROUND(g, h, a, b, c, d, e, f, K[(i) + 2], W[(i) + 2]);              \
    ROUND(f, g, h, a, b, c, d, e, K[(i) + 3], W[(i) + 3]);              \
    ROUND(e, f, g, h, a, b, c, d, K[(i) + 4], W[(i) + 4]);              \
    ROUND(d, e, f, g, h, a, b, c, K[(i) + 5], W[(i) + 5]);              \
    ROUND(c, d, e, f, g, h, a, b, K[(i) + 6], W[(i) + 6]);              \
    ROUND(b, c, d, e, f, g, h, a, K[(i) + 7], W[(i) + 7])

}  // namespace

void transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    for (int i = 0; i < 16; i++) W[i] = be32(block + 4 * i);
    for (int i = 16; i < 64; i++) W[i] = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i += 8) {
        ROUND8(i, W);
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint32_t st[8];
    memcpy(st, IV, sizeof(st));

    size_t full = len / 64;
    for (size_t i = 0; i < full; i++) transform(st, data + 64 * i);

    uint8_t tail[128] = {0};
    size_t rem = len - full * 64;
    memcpy(tail, data + full * 64, rem);
    tail[rem] = 0x80;
    size_t tailLen = rem < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) tail[tailLen - 1 - i] = (uint8_t)(bits >> (8 * i));
    transform(st, tail);
    if (tailLen == 128) transform(st, tail + 64);

    for (int i = 0; i < 8; i++) put_be32(out + 4 * i, st[i]);
}

void sha256d(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint8_t tmp[32];
    sha256(data, len, tmp);
    sha256(tmp, 32, out);
}

void prepare(MidState &ms, const uint8_t header[80]) {
    memcpy(ms.mid, IV, sizeof(ms.mid));
    transform(ms.mid, header);

    uint32_t *w = ms.w;
    w[0] = be32(header + 64);  // last 4 bytes of the merkle root
    w[1] = be32(header + 68);  // ntime
    w[2] = be32(header + 72);  // nbits
    w[3] = 0;                  // nonce, patched per hash
    w[4] = 0x80000000;
    for (int i = 5; i < 15; i++) w[i] = 0;
    w[15] = 640;  // 80 bytes * 8
    w[16] = s1(w[14]) + w[9] + s0(w[1]) + w[0];
    w[17] = s1(w[15]) + w[10] + s0(w[2]) + w[1];

    // Rounds 0..2 only depend on w[0..2] which are fixed for the job.
    uint32_t a = ms.mid[0], b = ms.mid[1], c = ms.mid[2], d = ms.mid[3];
    uint32_t e = ms.mid[4], f = ms.mid[5], g = ms.mid[6], h = ms.mid[7];
    ROUND(a, b, c, d, e, f, g, h, K[0], w[0]);
    ROUND(h, a, b, c, d, e, f, g, K[1], w[1]);
    ROUND(g, h, a, b, c, d, e, f, K[2], w[2]);
    // After 3 rounds the register roles are rotated by 3.
    ms.pre[0] = f; ms.pre[1] = g; ms.pre[2] = h; ms.pre[3] = a;
    ms.pre[4] = b; ms.pre[5] = c; ms.pre[6] = d; ms.pre[7] = e;
}

bool scan(const MidState &ms, uint32_t nonce, uint32_t limit, uint8_t out[32]) {
    uint32_t W[64];

    // ---- first hash, second block (header bytes 64..79 + padding) ----
    W[0] = ms.w[0];
    W[1] = ms.w[1];
    W[2] = ms.w[2];
    W[3] = bswap(nonce);
    W[4] = 0x80000000;
    for (int i = 5; i < 15; i++) W[i] = 0;
    W[15] = 640;
    W[16] = ms.w[16];
    W[17] = ms.w[17];
#pragma GCC unroll 46
    for (int i = 18; i < 64; i++) W[i] = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];

    uint32_t a = ms.pre[0], b = ms.pre[1], c = ms.pre[2], d = ms.pre[3];
    uint32_t e = ms.pre[4], f = ms.pre[5], g = ms.pre[6], h = ms.pre[7];
    // Resume at round 3: the rotating window is shifted by 3 positions.
    ROUND(a, b, c, d, e, f, g, h, K[3], W[3]);
    ROUND(h, a, b, c, d, e, f, g, K[4], W[4]);
    ROUND(g, h, a, b, c, d, e, f, K[5], W[5]);
    ROUND(f, g, h, a, b, c, d, e, K[6], W[6]);
    ROUND(e, f, g, h, a, b, c, d, K[7], W[7]);
    {
        // Rename so that ROUND8 can be reused for rounds 8..63.
        uint32_t A = d, B = e, C = f, D = g, E = h, F = a, G = b, H = c;
        a = A; b = B; c = C; d = D; e = E; f = F; g = G; h = H;
    }
    ROUND8(8, W);
    ROUND8(16, W);
    ROUND8(24, W);
    ROUND8(32, W);
    ROUND8(40, W);
    ROUND8(48, W);
    ROUND8(56, W);

    // ---- second hash of the 32-byte digest ----
    W[0] = ms.mid[0] + a;
    W[1] = ms.mid[1] + b;
    W[2] = ms.mid[2] + c;
    W[3] = ms.mid[3] + d;
    W[4] = ms.mid[4] + e;
    W[5] = ms.mid[5] + f;
    W[6] = ms.mid[6] + g;
    W[7] = ms.mid[7] + h;
    W[8] = 0x80000000;
    for (int i = 9; i < 15; i++) W[i] = 0;
    W[15] = 256;
#pragma GCC unroll 48
    for (int i = 16; i < 64; i++) W[i] = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];

    a = IV[0]; b = IV[1]; c = IV[2]; d = IV[3];
    e = IV[4]; f = IV[5]; g = IV[6]; h = IV[7];
    ROUND8(0, W);
    ROUND8(8, W);
    ROUND8(16, W);
    ROUND8(24, W);
    ROUND8(32, W);
    ROUND8(40, W);
    ROUND8(48, W);
    // Rounds 56..60. After round 60 the register that will end up as the
    // final `h` is already known (h64 = g63 = f62 = e61), which lets us reject
    // >99.99% of nonces without doing the last 3 rounds.
    ROUND(a, b, c, d, e, f, g, h, K[56], W[56]);
    ROUND(h, a, b, c, d, e, f, g, K[57], W[57]);
    ROUND(g, h, a, b, c, d, e, f, K[58], W[58]);
    ROUND(f, g, h, a, b, c, d, e, K[59], W[59]);
    ROUND(e, f, g, h, a, b, c, d, K[60], W[60]);
    // The logical `e` register now lives in variable `h`; three rounds later
    // it becomes the final `h`, so H7 = IV[7] + h.
    uint32_t h7 = IV[7] + h;
    if (bswap(h7) > limit) return false;

    ROUND(d, e, f, g, h, a, b, c, K[61], W[61]);
    ROUND(c, d, e, f, g, h, a, b, K[62], W[62]);
    ROUND(b, c, d, e, f, g, h, a, K[63], W[63]);

    put_be32(out + 0, IV[0] + a);
    put_be32(out + 4, IV[1] + b);
    put_be32(out + 8, IV[2] + c);
    put_be32(out + 12, IV[3] + d);
    put_be32(out + 16, IV[4] + e);
    put_be32(out + 20, IV[5] + f);
    put_be32(out + 24, IV[6] + g);
    put_be32(out + 28, IV[7] + h);
    return true;
}

}  // namespace sha
