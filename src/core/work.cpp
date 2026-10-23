#include "work.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace work {

namespace {

void putLE32(uint8_t *p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Little-endian 256-bit number as a double.
double leToDouble(const uint8_t v[32]) {
    double r = 0;
    for (int i = 31; i >= 0; i--) r = r * 256.0 + v[i];
    return r;
}

const double kDiff1 = 26959535291011309493156476344723991336010898738574164086137773096960.0;  // 0xFFFF * 2^208

}  // namespace

bool fromHex(const char *hex, uint8_t *out, size_t len) {
    if (strlen(hex) != len * 2) return false;
    for (size_t i = 0; i < len; i++) {
        int hi = hexNibble(hex[2 * i]), lo = hexNibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

bool fromHex(const char *hex, std::vector<uint8_t> &out) {
    size_t n = strlen(hex);
    if (n % 2) return false;
    out.resize(n / 2);
    return fromHex(hex, out.data(), out.size());
}

std::string toHex(const uint8_t *data, size_t len) {
    static const char *digits = "0123456789abcdef";
    std::string s(len * 2, '0');
    for (size_t i = 0; i < len; i++) {
        s[2 * i] = digits[data[i] >> 4];
        s[2 * i + 1] = digits[data[i] & 15];
    }
    return s;
}

uint32_t Job::blockHeight() const {
    // coinbase tx: version(4) | vin count(1) | prevout(36) | script len(1) | script...
    // BIP34: script starts with a push of the height (little-endian).
    if (coinb1.size() < 43) return 0;
    uint8_t n = coinb1[42];
    if (n < 1 || n > 4 || coinb1.size() < 43u + n) return 0;
    uint32_t h = 0;
    for (int i = 0; i < n; i++) h |= (uint32_t)coinb1[43 + i] << (8 * i);
    return h;
}

void build(const Job &job, uint64_t extranonce2, Work &out) {
    // extranonce2 is serialized big-endian-as-hex in the coinbase, any fixed
    // width; we just write the counter's bytes in order.
    std::vector<uint8_t> en2(job.extranonce2Size, 0);
    for (int i = 0; i < job.extranonce2Size && i < 8; i++)
        en2[job.extranonce2Size - 1 - i] = (uint8_t)(extranonce2 >> (8 * i));

    std::vector<uint8_t> coinbase;
    coinbase.reserve(job.coinb1.size() + job.extranonce1.size() + en2.size() + job.coinb2.size());
    coinbase.insert(coinbase.end(), job.coinb1.begin(), job.coinb1.end());
    coinbase.insert(coinbase.end(), job.extranonce1.begin(), job.extranonce1.end());
    coinbase.insert(coinbase.end(), en2.begin(), en2.end());
    coinbase.insert(coinbase.end(), job.coinb2.begin(), job.coinb2.end());

    uint8_t merkle[64];
    sha::sha256d(coinbase.data(), coinbase.size(), merkle);
    for (const auto &branch : job.branches) {
        memcpy(merkle + 32, branch.data(), 32);
        sha::sha256d(merkle, 64, merkle);
    }

    uint8_t *h = out.header;
    putLE32(h, job.version);
    memcpy(h + 4, job.prevHash, 32);
    memcpy(h + 36, merkle, 32);
    putLE32(h + 68, job.ntime);
    putLE32(h + 72, job.nbits);
    putLE32(h + 76, 0);

    out.jobId = job.id;
    out.extranonce2Hex = toHex(en2.data(), en2.size());
    out.ntime = job.ntime;
    out.shareDifficulty = job.difficulty;
    out.quickLimit = quickLimitFor(job.difficulty);
    targetFromBits(job.nbits, out.networkTarget);
    sha::prepare(out.mid, h);
}

double hashDifficulty(const uint8_t hash[32]) {
    // The 8 bytes from the most significant non-zero one carry more bits than
    // a double holds: one conversion instead of 32 multiply-adds (doubles are
    // emulated in software on the ESP32, and this runs for every candidate).
    int top = 31;
    while (top >= 0 && !hash[top]) top--;
    if (top < 0) return INFINITY;
    int low = top >= 7 ? top - 7 : 0;
    uint64_t v = 0;
    for (int i = top; i >= low; i--) v = v << 8 | hash[i];
    return kDiff1 / ldexp((double)v, 8 * low);
}

uint32_t quickLimitFor(double difficulty) {
    // target_top32 = 0xFFFF / (65536 * difficulty), rounded up so the quick
    // filter never rejects a valid share; the exact check is done afterwards.
    if (difficulty <= 0) return 0xFFFFFFFFu;
    double top = 65535.0 / (65536.0 * difficulty);
    if (top >= 4294967295.0) return 0xFFFFFFFFu;
    return (uint32_t)ceil(top);
}

void targetFromBits(uint32_t nbits, uint8_t target[32]) {
    memset(target, 0, 32);
    uint32_t exp = nbits >> 24;
    uint32_t mant = nbits & 0x007FFFFF;
    // target = mant * 256^(exp-3); byte i of the LE number.
    for (int i = 0; i < 3; i++) {
        int pos = (int)exp - 3 + i;
        if (pos >= 0 && pos < 32) target[pos] = (uint8_t)(mant >> (8 * i));
    }
}

double difficultyFromBits(uint32_t nbits) {
    uint8_t t[32];
    targetFromBits(nbits, t);
    double v = leToDouble(t);
    return v > 0 ? kDiff1 / v : 0;
}

bool meetsTarget(const uint8_t hash[32], const uint8_t target[32]) {
    for (int i = 31; i >= 0; i--) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true;
}

std::string formatSI(double v, int decimals) {
    static const char *units[] = {"", "k", "M", "G", "T", "P", "E", "Z", "Y"};
    int u = 0;
    while (fabs(v) >= 1000.0 && u < 8) {
        v /= 1000.0;
        u++;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f%s", decimals, v, units[u]);
    return buf;
}

}  // namespace work
