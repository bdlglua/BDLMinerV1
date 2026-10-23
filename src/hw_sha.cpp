#include "hw_sha.h"

#include <string.h>

#include "core/sha256.h"
#include "core/work.h"
#include "soc/soc_caps.h"

#if SOC_SHA_SUPPORT_RESUME

#include "hal/sha_ll.h"
#include "sha/sha_dma.h"

namespace hwsha {

bool available() { return true; }

static inline void waitIdle() {
    while (sha_ll_busy()) {
    }
}

void prepare(Job &job, const uint8_t header[80]) {
    // The engine works on raw message bytes, so the header is copied as-is.
    memcpy(job.tail, header + 64, 16);
    memset(&job.tail[4], 0, 48);
    ((uint8_t *)job.tail)[16] = 0x80;
    ((uint8_t *)job.tail)[62] = 0x02;  // 640 bits, big-endian length
    ((uint8_t *)job.tail)[63] = 0x80;

    esp_sha_acquire_hardware();
    sha_ll_fill_text_block(header, 16);
    sha_ll_start_block(SHA2_256);
    waitIdle();
    sha_ll_read_digest(SHA2_256, job.mid, 8);
    esp_sha_release_hardware();
}

uint32_t scan(Job &job, uint32_t first, uint32_t count, uint32_t limit,
              void (*hit)(uint32_t, const uint8_t *, void *), void *ctx) {
    uint32_t second[16] = {0};
    second[8] = 0x00000080;   // padding byte 0x80
    second[15] = 0x00010000;  // 256 bits, big-endian length
    volatile uint32_t *h7 = (volatile uint32_t *)(SHA_H_BASE + 28);

    esp_sha_acquire_hardware();
    uint32_t nonce = first;
    for (uint32_t i = 0; i < count; i++, nonce++) {
        job.tail[3] = nonce;
        sha_ll_write_digest(SHA2_256, job.mid, 8);
        sha_ll_fill_text_block(job.tail, 16);
        sha_ll_continue_block(SHA2_256);
        waitIdle();
        sha_ll_read_digest(SHA2_256, second, 8);

        sha_ll_fill_text_block(second, 16);
        sha_ll_start_block(SHA2_256);
        waitIdle();
        // Bytes 28..31 of the digest, read as a little-endian word, are the
        // most significant 32 bits of the hash as a 256-bit number.
        if (*h7 <= limit) {
            uint8_t hash[32];
            sha_ll_read_digest(SHA2_256, hash, 8);
            hit(nonce, hash, ctx);
        }
    }
    esp_sha_release_hardware();
    return count;
}

}  // namespace hwsha

#else  // no resumable SHA peripheral (original ESP32)

namespace hwsha {

bool available() { return false; }
void prepare(Job &, const uint8_t *) {}
uint32_t scan(Job &, uint32_t, uint32_t, uint32_t, void (*)(uint32_t, const uint8_t *, void *), void *) {
    return 0;
}

}  // namespace hwsha

#endif

namespace hwsha {

namespace {
struct TestCtx {
    uint32_t nonce;
    uint8_t hash[32];
    int hits;
};
}  // namespace

bool selfTest() {
    if (!available()) return false;

    static const char *kGenesis =
        "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e"
        "67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c";
    const uint32_t kNonce = 2083236893;
    uint8_t header[80];
    work::fromHex(kGenesis, header, 80);

    Job job;
    prepare(job, header);
    TestCtx t{};
    scan(job, kNonce - 1000, 2000, work::quickLimitFor(1.0),
         [](uint32_t nonce, const uint8_t hash[32], void *ctx) {
             TestCtx *t = (TestCtx *)ctx;
             t->nonce = nonce;
             memcpy(t->hash, hash, 32);
             t->hits++;
         },
         &t);
    if (t.hits != 1 || t.nonce != kNonce) return false;

    uint8_t ref[32];
    sha::sha256d(header, 80, ref);
    return memcmp(ref, t.hash, 32) == 0;
}

}  // namespace hwsha
