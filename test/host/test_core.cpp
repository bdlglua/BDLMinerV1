// Host-side tests for the portable mining core.
//   ./test/host/run.sh
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <chrono>

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
    CHECK(p.handleLine(R"({"id":11,"result":null,"error":[23,"Low difficulty share",null]})") == stratum::Event::ShareRejected);
    CHECK(p.lastError() == "Low difficulty share");
}

int main() {
    testSha();
    testStratumJob();
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
