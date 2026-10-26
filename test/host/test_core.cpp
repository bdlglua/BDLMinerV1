// Host-side tests for the portable mining core.
//   ./test/host/run.sh
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <ArduinoJson.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/address.h"
#include "core/fill.h"
#include "core/sha256.h"
#include "core/stratum.h"
#include "core/work.h"

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                \
        }                                                              \
    } while (0)

static std::string reversedHex(const uint8_t h[32]) {
    uint8_t r[32];
    for (int i = 0; i < 32; i++) r[i] = h[31 - i];
    return work::toHex(r, 32);
}

// Bitcoin genesis block header.
static const char *kGenesisHeader =
    "01000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
    "29ab5f49"
    "ffff001d"
    "1dac2b7c";
static const char *kGenesisHash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
static const uint32_t kGenesisNonce = 2083236893;

static void testSha() {
    uint8_t out[32];
    sha::sha256((const uint8_t *)"abc", 3, out);
    CHECK(work::toHex(out, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // Multi-block message (56..63 byte tail edge case).
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha::sha256((const uint8_t *)m, strlen(m), out);
    CHECK(work::toHex(out, 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    uint8_t header[80];
    CHECK(work::fromHex(kGenesisHeader, header, 80));
    sha::sha256d(header, 80, out);
    CHECK(reversedHex(out) == kGenesisHash);

    sha::MidState ms;
    sha::prepare(ms, header);
    uint8_t h2[32];
    CHECK(sha::scan(ms, kGenesisNonce, 0xFFFFFFFF, h2));
    CHECK(memcmp(out, h2, 32) == 0);

    // Exercise the prepared rounds and early rejection with changing midstates,
    // padding words, nonce byte order/wrap and exact equality at the top word.
    uint32_t seed = 0x93ac5f17;
    for (int job = 0; job < 64; ++job) {
        uint8_t varied[80];
        for (auto &byte : varied) byte = (seed = seed * 1664525u + 1013904223u) >> 24;
        if (job < 2) memset(varied, job ? 0xff : 0, sizeof(varied));
        sha::MidState prepared;
        sha::prepare(prepared, varied);
        for (int i = 0; i < 32; ++i) {
            uint32_t nonce = i < 4 ? uint32_t(i - 2) : (seed = seed * 1664525u + 1013904223u);
            for (int b = 0; b < 4; ++b) varied[76 + b] = nonce >> (8 * b);
            uint8_t ref[32], fast[32];
            sha::sha256d(varied, sizeof(varied), ref);
            uint32_t top = uint32_t(ref[28]) | uint32_t(ref[29]) << 8 |
                           uint32_t(ref[30]) << 16 | uint32_t(ref[31]) << 24;
            const uint32_t limits[] = {0, 1, 0xffff, 0x0fffffff, UINT32_MAX, top, top ? top - 1 : 0};
            for (uint32_t limit : limits) {
                memset(fast, 0xa5, sizeof(fast));
                bool hit = sha::scan(prepared, nonce, limit, fast);
                CHECK(hit == (top <= limit));
                if (hit) CHECK(memcmp(fast, ref, sizeof(ref)) == 0);
                else for (auto byte : fast) CHECK(byte == 0xa5);
            }
        }
    }
    puts("SHA-256d: 2,048 varied headers/nonces and 14,336 exact filter checks passed");

    // Fast path must agree with the reference on random nonces.
    for (uint32_t n = 0; n < 2000; n++) {
        uint8_t ref[32], fast[32];
        uint32_t nonce = n * 2654435761u;
        memcpy(header + 76, &nonce, 4);  // little-endian host
        sha::sha256d(header, 80, ref);
        CHECK(sha::scan(ms, nonce, 0xFFFFFFFF, fast));
        CHECK(memcmp(ref, fast, 32) == 0);
        uint32_t top = ref[28] | ref[29] << 8 | ref[30] << 16 | (uint32_t)ref[31] << 24;
        CHECK(sha::scan(ms, nonce, top, fast) == true);
        if (top > 0) CHECK(sha::scan(ms, nonce, top - 1, fast) == false);
        if (failures) break;
    }

    // Scanning around the genesis nonce at difficulty 1 finds exactly it.
    uint32_t lim = work::quickLimitFor(1.0);
    CHECK(lim == 1);
    int found = 0;
    for (uint32_t n = kGenesisNonce - 50000; n < kGenesisNonce + 50000; n++) {
        if (sha::scan(ms, n, lim, h2) && work::hashDifficulty(h2) >= 1.0) {
            CHECK(n == kGenesisNonce);
            found++;
        }
    }
    CHECK(found == 1);
    sha::scan(ms, kGenesisNonce, lim, h2);
    double d = work::hashDifficulty(h2);
    printf("genesis share difficulty: %.2f\n", d);
    CHECK(d > 2536 && d < 2537);

    uint8_t target[32];
    work::targetFromBits(0x1d00ffff, target);
    CHECK(work::meetsTarget(h2, target));
    CHECK(fabs(work::difficultyFromBits(0x1d00ffff) - 1.0) < 1e-9);

    auto t0 = std::chrono::steady_clock::now();
    const uint32_t N = 2000000;
    for (uint32_t n = 0; n < N; n++) sha::scan(ms, n, 0, h2);
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("host software hashrate: %.2f MH/s\n", N / s / 1e6);
}

// Real block 125552 (the one used in the Bitcoin wiki hashing example) built
// from Stratum-style fields, to check header assembly + byte orders.
// pool.nerdminers.org answers an unknown user agent this way.
static void testSubscribeRefused() {
    stratum::Protocol p;
    CHECK(p.handleLine(R"({"result":"Only allowed user agents may subscribe","id":1,"error":null})") ==
          stratum::Event::SubscribeFailed);
    CHECK(p.lastError() == "Only allowed user agents may subscribe");
    CHECK(p.handleLine(R"({"id":1,"result":null,"error":[20,"Other error",null]})") ==
          stratum::Event::SubscribeFailed);
    CHECK(p.lastError() == "Other error");
}

// hashDifficulty() against the exact 256-bit value, for hashes with 0 to 31
// leading zero bytes.
static void testHashDifficulty() {
    const double kDiff1 = 26959535291011309493156476344723991336010898738574164086137773096960.0;
    uint32_t seed = 12345;
    double worst = 0;
    for (int k = 0; k < 100000; k++) {
        uint8_t h[32];
        for (auto &b : h) b = (seed = seed * 1103515245u + 12345u) >> 16;
        for (int z = 0, zeros = k % 32; z < zeros; z++) h[31 - z] = 0;
        double v = 0;
        for (int i = 31; i >= 0; i--) v = v * 256.0 + h[i];
        double ref = kDiff1 / v, got = work::hashDifficulty(h);
        worst = std::max(worst, fabs(got / ref - 1));
    }
    CHECK(worst < 1e-15);
    uint8_t zero[32] = {0};
    CHECK(std::isinf(work::hashDifficulty(zero)));
}

// ---------------------------------------------------------------------------
// The software SHA-256d of src/hw_sha_fill.S: its instructions (the lines
// tagged "// F<body><n>") are run here on the buffers of fill::prepare(), and
// the candidates it reports are compared with sha::scan().
// ---------------------------------------------------------------------------

namespace hwfill {

// Ctx offsets, as in src/hw_sha.cpp and tools/hw_fill/gen_hw_fill.py.
enum { FILLP = 100, Q = 104, HITQ = 108, HITF = 112, END1 = 116, END2 = 120, B1 = 124, B2 = 128, B1_61 = 132,
       IV7 = 136, C18 = 140, MID = 144, CTX_BYTES = 176 };
const uint32_t kCtx = 0x10000, kB1 = 0x20000, kB2 = 0x30000;

struct Ins {
    std::string op;
    std::vector<std::string> args;
};

std::map<char, std::vector<Ins>> load(const char *path) {
    std::map<char, std::vector<std::pair<int, Ins>>> tagged;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t t = line.find("// F");
        if (t == std::string::npos || t + 8 > line.size()) continue;
        char body = line[t + 4];
        int n = atoi(line.c_str() + t + 5);
        std::string code = line.substr(0, t);
        std::istringstream ss(code);
        Ins ins;
        ss >> ins.op;
        std::string rest;
        std::getline(ss, rest);
        std::string arg;
        for (char c : rest + ",") {
            if (c == ',') {
                if (!arg.empty()) ins.args.push_back(arg);
                arg.clear();
            } else if (c != ' ' && c != '\t') {
                arg += c;
            }
        }
        tagged[body].push_back({n, ins});
    }
    std::map<char, std::vector<Ins>> out;
    for (auto &b : tagged) {
        std::sort(b.second.begin(), b.second.end(), [](auto &x, auto &y) { return x.first < y.first; });
        for (auto &x : b.second) out[b.first].push_back(x.second);
    }
    return out;
}

struct Machine {
    uint32_t a[16] = {0};
    uint32_t sar = 0;
    uint8_t ctx[CTX_BYTES] = {0};
    fill::Buffers *buf;
    bool fault = false;

    uint32_t *word(uint32_t addr) {
        if (addr % 4) fault = true;
        else if (addr >= kCtx && addr < kCtx + CTX_BYTES) return (uint32_t *)(ctx + addr - kCtx);
        else if (addr >= kB1 && addr < kB1 + sizeof(buf->b1)) return &buf->b1[(addr - kB1) / 4];
        else if (addr >= kB2 && addr < kB2 + sizeof(buf->b2)) return &buf->b2[(addr - kB2) / 4];
        fault = true;
        static uint32_t junk;
        return &junk;
    }
    uint32_t &ctxw(int off) { return *word(kCtx + off); }
    uint32_t &r(const std::string &name) { return a[atoi(name.c_str() + 1)]; }
    uint32_t imm(const std::string &s) { return (uint32_t)strtol(s.c_str(), nullptr, 0); }

    // Runs a body until its end; returns the branch target taken (".LTA",
    // ".LTB") or "" at the end.
    std::string run(const std::vector<Ins> &body) {
        for (size_t i = 0; i < body.size() && !fault; i++) {
            const Ins &x = body[i];
            const auto &g = x.args;
            if (x.op.back() == ':') continue;
            if (x.op == "l32i") r(g[0]) = *word(r(g[1]) + imm(g[2]));
            else if (x.op == "s32i") *word(r(g[1]) + imm(g[2])) = r(g[0]);
            else if (x.op == "add") r(g[0]) = r(g[1]) + r(g[2]);
            else if (x.op == "sub") r(g[0]) = r(g[1]) - r(g[2]);
            else if (x.op == "addi") r(g[0]) = r(g[1]) + imm(g[2]);
            else if (x.op == "xor") r(g[0]) = r(g[1]) ^ r(g[2]);
            else if (x.op == "and") r(g[0]) = r(g[1]) & r(g[2]);
            else if (x.op == "or") r(g[0]) = r(g[1]) | r(g[2]);
            else if (x.op == "mov") r(g[0]) = r(g[1]);
            else if (x.op == "movi.n" || x.op == "movi") r(g[0]) = imm(g[1]);
            else if (x.op == "ssai") sar = imm(g[0]);
            else if (x.op == "src") r(g[0]) = (uint32_t)((((uint64_t)r(g[1])) << 32 | r(g[2])) >> sar);
            else if (x.op == "srli") r(g[0]) = r(g[1]) >> imm(g[2]);
            else if (x.op == "extui") r(g[0]) = (r(g[1]) >> imm(g[2])) & ((1u << imm(g[3])) - 1);
            else if (x.op == "beqz" || x.op == "bnez") {
                bool taken = (r(g[0]) == 0) == (x.op == "beqz");
                if (!taken) continue;
                if (g[1].rfind(".LT", 0) == 0) return g[1];
                while (++i < body.size() && body[i].op != g[1] + ":") {
                }
            } else {
                printf("hwfill: unknown instruction %s\n", x.op.c_str());
                fault = true;
            }
        }
        return "";
    }
};

// Candidates (positions, i.e. byte-swapped nonces) the generated code reports
// for `count` nonces from position q0.
std::vector<uint32_t> run(std::map<char, std::vector<Ins>> &prog, const uint8_t header[80], uint32_t q0, int count,
                          bool *fault) {
    static fill::Buffers buf;
    fill::prepare(buf, header);
    Machine m;
    m.buf = &buf;
    m.a[3] = kCtx;
    m.ctxw(END1) = kB1 + fill::kSteps * fill::kRecordBytes;
    m.ctxw(END2) = kB2 + fill::kSteps * fill::kRecordBytes;
    m.ctxw(B1) = kB1;
    m.ctxw(B2) = kB2;
    m.ctxw(B1_61) = kB1 + 61 * fill::kRecordBytes;
    m.ctxw(IV7) = sha::IV[7];
    m.ctxw(C18) = buf.c18;
    for (int i = 0; i < 8; i++) m.ctxw(MID + 4 * i) = buf.mid[i];
    m.ctxw(Q) = q0 - 1;
    m.a[9] = m.ctxw(END2);  // as after a second hash: the first iteration starts nonce q0
    std::vector<uint32_t> hits;
    // A nonce's result is checked by the transition that starts the next one.
    while (!m.fault && (int32_t)(m.ctxw(Q) - q0) < count) {
        std::string to = m.run(prog['M']);
        if (to == ".LTA") m.run(prog['A']);
        if (to == ".LTB") m.run(prog['B']);
        if (m.ctxw(HITF)) {
            hits.push_back(m.ctxw(HITQ));
            m.ctxw(HITF) = 0;
        }
    }
    *fault = m.fault;
    return hits;
}

}  // namespace hwfill

static void testHwFill() {
    auto prog = hwfill::load("src/hw_sha_fill.S");
    CHECK(prog['M'].size() > 100 && prog['A'].size() > 20 && prog['B'].size() > 20);
    if (prog['M'].empty()) return;

    // Reference: positions in [q0, q0 + count) whose hash has 16 leading zero bits.
    auto expected = [](const uint8_t header[80], uint32_t q0, int count) {
        sha::MidState ms;
        sha::prepare(ms, header);
        std::vector<uint32_t> out;
        uint8_t h[32];
        for (int i = 0; i < count; i++)
            if (sha::scan(ms, __builtin_bswap32(q0 + i), 0x0000FFFF, h)) out.push_back(q0 + i);
        return out;
    };

    // The known-answer header of hw_sha.cpp, around its first candidate.
    uint8_t kat[80] = {0};
    for (int i = 0; i < 19; i++) {
        uint32_t w = 0x9E3779B9u * (i + 1);
        kat[4 * i] = w >> 24;
        kat[4 * i + 1] = w >> 16;
        kat[4 * i + 2] = w >> 8;
        kat[4 * i + 3] = w;
    }
    bool fault = false;
    uint32_t q0 = 0x1000 + 0xa94c - 3;
    auto got = hwfill::run(prog, kat, q0, 8, &fault);
    CHECK(!fault);
    CHECK(got == expected(kat, q0, 8));
    CHECK(got.size() == 1 && got[0] == 0x1000 + 0xa94c);

    // Random headers: around a candidate found by brute force, plus a run of
    // 200 nonces that must match nonce for nonce.
    uint32_t seed = 777;
    int candidates = 0;
    for (int k = 0; k < 6; k++) {
        uint8_t h[80];
        for (auto &b : h) b = (seed = seed * 1103515245u + 12345u) >> 16;
        sha::MidState ms;
        sha::prepare(ms, h);
        uint8_t out[32];
        uint32_t q = seed;
        while (!sha::scan(ms, __builtin_bswap32(q), 0x0000FFFF, out)) q++;
        got = hwfill::run(prog, h, q - 2, 6, &fault);
        CHECK(!fault);
        CHECK(got == expected(h, q - 2, 6));
        candidates += got.size();
        got = hwfill::run(prog, h, q + 1, 200, &fault);
        CHECK(!fault);
        CHECK(got == expected(h, q + 1, 200));
    }
    CHECK(candidates >= 6);
    printf("hw fill: software hash of hw_sha_fill.S matches sha::scan (%d candidates)\n", candidates);
}

// Malformed or early messages must not crash or start mining on nothing.
static void testStratumRobustness() {
    stratum::Protocol p;
    // set_extranonce before any job: no job to publish.
    CHECK(p.handleLine(R"({"id":null,"method":"mining.set_extranonce","params":["aabb",4]})") == stratum::Event::None);
    CHECK(!p.hasJob());
    CHECK(p.handleLine(R"({"id":null,"method":"mining.set_extranonce","params":["zz",4]})") == stratum::Event::Error);
    CHECK(p.job().extranonce1.size() == 2);  // the bad one did not overwrite it
    CHECK(p.handleLine(R"({"id":null,"method":"mining.set_extranonce","params":["aabb",0]})") == stratum::Event::Error);
    // Unusable extranonce2 sizes are refused at subscribe time.
    CHECK(p.handleLine(R"({"id":1,"result":[[],"08000002",-1],"error":null})") == stratum::Event::SubscribeFailed);
    CHECK(p.handleLine(R"({"id":1,"result":[[],"08000002",64],"error":null})") == stratum::Event::SubscribeFailed);
    // A job id that is not a string.
    const char *notify =
        R"({"params":[12,"ab02cd818b9e567ee21793cddef299feb29ad444a41b85b8000008a300000000",)"
        R"("01000000","00000000",[],"00000001","1a44b9f2","4dd7f5c7",true],"id":null,"method":"mining.notify"})";
    CHECK(p.handleLine(notify) == stratum::Event::Error);
    CHECK(!p.hasJob());
}

static const char *kNotify =
    R"({"method":"mining.notify","params":["job-a",)"
    R"("ab02cd818b9e567ee21793cddef299feb29ad444a41b85b8000008a300000000",)"
    R"("01000000","00000000",[],"00000001","1a44b9f2","4dd7f5c7",true]})";

static void testStratumValidation() {
    stratum::Protocol p;
    for (const char *line : {R"({"id":2,"result":"true","error":null})",
                             R"({"id":2,"result":1,"error":null})",
                             R"({"id":2,"result":[],"error":null})",
                             R"({"id":2,"result":{},"error":null})"}) {
        p.reset();
        CHECK(p.handleLine(line) == stratum::Event::AuthFailed);
        CHECK(!p.authorized() && !p.lastError().empty());
    }
    CHECK(p.handleLine(R"({"id":2,"result":true,"error":null})") == stratum::Event::Authorized);
    CHECK(p.handleLine(R"({"id":2,"result":true,"error":null})") == stratum::Event::None);
    CHECK(p.authorized());
    p.reset();
    CHECK(p.handleLine(R"({"id":1,"result":[[],"aabb",4],"error":null})") == stratum::Event::Subscribed);

    // A broken subscribe response must not erase or partly change the session.
    const char *badSubscriptions[] = {
        R"({"id":1,"result":[],"error":null})",
        R"({"id":1,"result":[[],null,4],"error":null})",
        R"({"id":1,"result":[[],"ccdd"],"error":null})",
        R"({"id":1,"result":[[],"ccdd",64],"error":null})",
        R"({"id":1,"result":[[],"11zz",4],"error":null})",
    };
    for (const char *line : badSubscriptions) {
        CHECK(p.handleLine(line) == stratum::Event::SubscribeFailed);
        CHECK(work::toHex(p.job().extranonce1.data(), p.job().extranonce1.size()) == "aabb");
        CHECK(p.job().extranonce2Size == 4);
        CHECK(!p.lastError().empty());
    }

    CHECK(p.handleLine(kNotify) == stratum::Event::NewJob);
    work::Work original;
    work::build(p.job(), 7, original);
    struct BadField { int index; const char *json; } badFields[] = {
        {0, "12"}, {0, "null"}, {0, R"("")"},
        {0, R"("0123456789012345678901234567890123456789012345678901234567890123456789")"},
        {1, R"("00")"}, {2, "null"}, {3, "false"},
        {4, "null"}, {4, "{}"}, {4, R"(["11"])"},
        {5, R"("0000000z")"}, {5, R"("100000000")"}, {5, "1"},
        {6, R"("1a44b9f2junk")"}, {6, "null"},
        {7, R"("-0000001")"}, {7, R"("4dd7f5c")"}, {8, R"("false")"},
    };
    for (const auto &bad : badFields) {
        JsonDocument notify, value;
        CHECK(!deserializeJson(notify, kNotify));
        CHECK(!deserializeJson(value, bad.json));
        notify["params"][bad.index].set(value.as<JsonVariant>());
        std::string line;
        serializeJson(notify, line);
        CHECK(p.handleLine(line.c_str()) == stratum::Event::Error);
        CHECK(p.hasJob());
        CHECK(p.job().id == "job-a");
        CHECK(!p.lastError().empty());
        work::Work current;
        work::build(p.job(), 7, current);
        CHECK(memcmp(original.header, current.header, sizeof(original.header)) == 0);
    }
    JsonDocument oversized;
    CHECK(!deserializeJson(oversized, kNotify));
    for (int i = 0; i < 33; ++i) oversized["params"][4].add(std::string(64, '0'));
    std::string line;
    serializeJson(oversized, line);
    CHECK(p.handleLine(line.c_str()) == stratum::Event::Error);
    CHECK(p.job().branches.empty());  // keep the previous valid job
}

static void testStratumSessionUpdates() {
    stratum::Protocol p;
    CHECK(p.handleLine(R"({"id":1,"result":[[],"aabb",4],"error":null})") == stratum::Event::Subscribed);
    CHECK(p.handleLine(kNotify) == stratum::Event::NewJob);
    CHECK(p.job().difficulty == 1);
    work::Work original;
    work::build(p.job(), 7, original);

    // Session updates take effect at the next notify, including when the
    // current job is clean. They must not rebuild the old job with new data.
    CHECK(p.handleLine(R"({"method":"mining.set_difficulty","params":[0.0005]})") == stratum::Event::Difficulty);
    CHECK(p.difficulty() == 0.0005);
    CHECK(p.job().difficulty == 1);
    CHECK(p.handleLine(R"({"method":"mining.set_extranonce","params":["ccdd",8]})") == stratum::Event::None);
    CHECK(p.job().extranonce2Size == 4);
    work::Work current;
    work::build(p.job(), 7, current);
    CHECK(memcmp(original.header, current.header, sizeof(original.header)) == 0);

    CHECK(p.handleLine(R"({"method":"mining.set_difficulty","params":[1e999]})") == stratum::Event::Error);
    CHECK(p.handleLine(R"({"method":"mining.set_extranonce","params":[null,4]})") == stratum::Event::Error);
    CHECK(p.handleLine(R"({"method":"mining.set_extranonce","params":["1122"]})") == stratum::Event::Error);
    CHECK(p.handleLine(kNotify) == stratum::Event::NewJob);
    CHECK(p.job().difficulty == 0.0005);
    CHECK(p.job().extranonce2Size == 8);
    CHECK(work::toHex(p.job().extranonce1.data(), p.job().extranonce1.size()) == "ccdd");

    p.reset();
    CHECK(!p.hasJob());
    CHECK(p.handleLine(kNotify) == stratum::Event::NewJob);
    CHECK(p.job().difficulty == 1);
    CHECK(p.job().extranonce2Size == 4);
    CHECK(p.job().extranonce1.empty());

    // Exact integer difficulties must not be rounded to seven digits by
    // the workaround for ArduinoJson's compact floating-point storage.
    CHECK(p.handleLine(R"({"method":"mining.set_difficulty","params":[16777216]})") == stratum::Event::Difficulty);
    CHECK(p.difficulty() == 16777216);
    CHECK(p.job().difficulty == 1);
    CHECK(p.handleLine(kNotify) == stratum::Event::NewJob);
    CHECK(p.job().difficulty == 16777216);
}

static void testStratumJob() {
    stratum::Protocol p;
    p.setCredentials("addr.worker", "x");

    CHECK(p.handleLine(R"({"id":1,"result":[[["mining.notify","ae6812eb4cd7735a302a8a9dd95cf71f"]],"08000002",4],"error":null})") ==
          stratum::Event::Subscribed);
    CHECK(p.job().extranonce1.size() == 4);
    CHECK(p.job().extranonce2Size == 4);
    CHECK(p.handleLine(R"({"id":null,"method":"mining.set_difficulty","params":[0.0005]})") == stratum::Event::Difficulty);
    CHECK(p.handleLine(R"({"id":2,"result":true,"error":null})") == stratum::Event::Authorized);

    // prevhash/version/ntime/nbits as a pool would send them for block 125552
    // (the coinbase is synthetic, so the merkle root is not checked here).
    const char *notify =
        R"({"params":["bf","ab02cd818b9e567ee21793cddef299feb29ad444a41b85b8000008a300000000",)"
        R"("01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020862062f503253482f04b8864e5008",)"
        R"("072f736c7573682f000000000100f2052a010000001976a914d23fcdf86f7e756a64a7a9688ef9903327048ed988ac00000000",)"
        R"([],"00000001","1a44b9f2","4dd7f5c7",true],"id":null,"method":"mining.notify"})";
    CHECK(p.handleLine(notify) == stratum::Event::NewJob);
    const work::Job &j = p.job();
    CHECK(j.clean);
    CHECK(j.difficulty == 0.0005);
    CHECK(j.blockHeight() == 0x6208);  // script starts with push(2) 08 62

    work::Work w;
    work::build(j, 0, w);
    // Expected header prefix of block 125552.
    const char *expectVersionPrev =
        "01000000"
        "81cd02ab7e569e8bcd9317e2fe99f2de44d49ab2b8851ba4a308000000000000";
    CHECK(work::toHex(w.header, 36) == expectVersionPrev);
    CHECK(work::toHex(w.header + 68, 8) == "c7f5d74df2b9441a");
    CHECK(w.extranonce2Hex == "00000000");

    // Merkle root of block 125552 from its known header.
    const char *h125552 =
        "0100000081cd02ab7e569e8bcd9317e2fe99f2de44d49ab2b8851ba4a308000000000000"
        "e320b6c2fffc8d750423db8b1eb942ae710e951ed797f7affc8892b0f1fc122b"
        "c7f5d74df2b9441a42a14695";
    uint8_t hdr[80], out[32];
    CHECK(work::fromHex(h125552, hdr, 80));
    sha::sha256d(hdr, 80, out);
    CHECK(reversedHex(out) == "00000000000000001e8d6829a8a21adc5d38d0a473b144b6765798e61f98bd1d");

    stratum::Share s{};
    strcpy(s.jobId, "bf");
    strcpy(s.extranonce2, "00000001");
    s.ntime = 0x4dd7f5c7;
    s.nonce = 0x9546a142;
    std::string line = p.submit(s);
    CHECK(line == "{\"id\":10,\"method\":\"mining.submit\",\"params\":[\"addr.worker\",\"bf\",\"00000001\",\"4dd7f5c7\",\"9546a142\"]}\n");
    CHECK(p.handleLine(R"({"id":10,"result":true,"error":null})") == stratum::Event::ShareAccepted);
    CHECK(p.handleLine(R"({"id":10,"result":true,"error":null})") == stratum::Event::None);
    CHECK(p.handleLine(R"({"id":999,"result":true,"error":null})") == stratum::Event::None);
    p.submit(s);
    CHECK(p.handleLine(R"({"id":11,"result":null,"error":[23,"Low difficulty share",null]})") == stratum::Event::ShareRejected);
    CHECK(p.lastError() == "Low difficulty share");
    CHECK(p.handleLine(R"({"id":11,"result":false,"error":null})") == stratum::Event::None);
    CHECK(p.pendingShares() == 0);
    for (size_t i = 0; i < stratum::Protocol::kMaxPendingShares; ++i) CHECK(!p.submit(s).empty());
    CHECK(!p.canSubmit() && p.submit(s).empty());
    CHECK(p.handleLine(R"({"id":13,"result":{},"error":null})") == stratum::Event::Error);
    CHECK(p.pendingShares() == stratum::Protocol::kMaxPendingShares);
    CHECK(p.handleLine(R"({"id":"13","result":true,"error":null})") == stratum::Event::None);
    CHECK(p.handleLine(R"({"id":13.5,"result":true,"error":null})") == stratum::Event::None);
    CHECK(p.handleLine(R"({"id":13,"result":true,"error":null})") == stratum::Event::ShareAccepted);
    CHECK(p.canSubmit());
    p.reset();
    CHECK(p.pendingShares() == 0);
    CHECK(p.handleLine(R"({"id":12,"result":true,"error":null})") == stratum::Event::None);
}

// Known-good addresses (genesis coinbase, BIP13, BIP173 and BIP350 vectors)
// and the typos the check must catch.
static void testAddress() {
    using address::Network;
    using address::Type;
    struct Case {
        const char *user;
        Type type;
        Network net;
    } cases[] = {
        {"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", Type::P2PKH, Network::Main},
        {"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa.rig1", Type::P2PKH, Network::Main},
        {"3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy", Type::P2SH, Network::Main},
        {"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", Type::P2WPKH, Network::Main},
        {"BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4", Type::P2WPKH, Network::Main},
        {"bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3", Type::P2WSH, Network::Main},
        {"bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0", Type::P2TR, Network::Main},
        {"tb1qw508d6qejxtdg4y5r3zarvary0c5xw7kxpjzsx", Type::P2WPKH, Network::Test},
        // Invalid: one character changed, mixed case, Bech32 checksum on a
        // v1 address, wrong prefix, account name instead of an address.
        {"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNb", Type::Invalid, Network::Unknown},
        {"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t5", Type::Invalid, Network::Unknown},
        {"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kV8F3T4", Type::Invalid, Network::Unknown},
        {"bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqh2y7hd", Type::Invalid, Network::Unknown},
        {"ltc1qw508d6qejxtdg4y5r3zarvary0c5xw7kgmn4n9", Type::Invalid, Network::Unknown},
        {"myaccount.worker1", Type::Invalid, Network::Unknown},
        {"", Type::Invalid, Network::Unknown},
    };
    for (const Case &c : cases) {
        address::Info info = address::parse(c.user);
        if (info.type != c.type || info.network != c.net) {
            printf("FAIL address %s -> %s\n", c.user, address::typeName(info.type));
            failures++;
        }
    }
}

int main() {
    testSha();
    testHashDifficulty();
    testHwFill();
    testSubscribeRefused();
    testStratumRobustness();
    testStratumValidation();
    testStratumSessionUpdates();
    testStratumJob();
    testAddress();
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
