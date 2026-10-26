#!/usr/bin/env python3
"""Run the production resumable SHA loop against a register-level host model."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/hw_sha.cpp').read_text()
start = source.index('namespace hwsha {', source.index('#if SOC_SHA_SUPPORT_RESUME'))
body = source[start:source.index('#elif CONFIG_IDF_TARGET_ESP32')]
common = source[source.rindex('namespace hwsha {'):]
prefix = r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "hw_sha.h"
#include "core/work.h"
#define SOC_SHA_SUPPORT_RESUME 1
uint32_t textRegisters[16], digestRegisters[8];
size_t textWrites = 0;
int locks = 0;
bool clobberText = false;
int fallbacks = 0;
void logf(const char *, ...) { ++fallbacks; }
constexpr int SHA2_256 = 0;
#define SHA_TEXT_BASE (uintptr_t(textRegisters))
#define SHA_H_BASE (uintptr_t(digestRegisters))
void writeRegister(volatile uint32_t *address, uint32_t value) {
    assert(locks > 0);
    assert(address >= textRegisters && address < textRegisters + 16);
    *address = value;
    ++textWrites;
}
#define REG_WRITE(address, value) writeRegister(address, value)
void esp_sha_acquire_hardware() { ++locks; }
void esp_sha_release_hardware() { assert(locks > 0); --locks; }
bool sha_ll_busy() { return false; }
void sha_ll_fill_text_block(const void *data, size_t words) {
    for (size_t i = 0; i < words; ++i) writeRegister(textRegisters + i, ((const uint32_t *)data)[i]);
}
void sha_ll_read_digest(int, void *out, size_t words) { memcpy(out, digestRegisters, words * 4); }
void sha_ll_write_digest(int, void *data, size_t words) { memcpy(digestRegisters, data, words * 4); }
void compress(bool start) {
    assert(locks > 0);
    uint32_t state[8];
    for (int i = 0; i < 8; ++i) state[i] = start ? sha::IV[i] : __builtin_bswap32(digestRegisters[i]);
    sha::transform(state, (const uint8_t *)textRegisters);
    for (int i = 0; i < 8; ++i) digestRegisters[i] = __builtin_bswap32(state[i]);
    if (clobberText) for (int i = 9; i < 15; ++i) textRegisters[i] = 0xdeadbeef;
}
void sha_ll_start_block(int) { compress(true); }
void sha_ll_continue_block(int) { compress(false); }
'''
tests = r'''
struct Hit { uint32_t nonce; uint8_t digest[32]; };
void hit(uint32_t nonce, const uint8_t *digest, void *ctx) {
    Hit value{}; value.nonce = nonce; memcpy(value.digest, digest, 32);
    ((std::vector<Hit> *)ctx)->push_back(value);
}
int main() {
    uint32_t seed = 0xa5b6c7d8;
    for (int k = 0; k < 16; ++k) {
        uint8_t header[80];
        for (auto &byte : header) byte = (seed = seed * 1664525u + 1013904223u) >> 24;
        hwsha::Job job;
        hwsha::prepare(job, header);
        assert(locks == 0);
        for (uint32_t limit : {UINT32_MAX, 0x7fffffffu, 0xffffu, 0u}) {
            uint32_t first = k % 2 ? UINT32_MAX - 5 : 100;
            constexpr uint32_t count = 19;
            // A different SHA user may leave nonzero padding between batches.
            for (auto &word : textRegisters) word = 0xa5a5a5a5;
            textWrites = 0;
            std::vector<Hit> hits;
            assert(hwsha::scan(job, first, count, limit, hit, &hits) == count);
            assert(textWrites == 6 + 20 * count && locks == 0);
            size_t found = 0;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t nonce = first + i;
                for (int b = 0; b < 4; ++b) header[76 + b] = nonce >> (8 * b);
                uint8_t ref[32]; sha::sha256d(header, sizeof(header), ref);
                uint32_t top = uint32_t(ref[28]) | uint32_t(ref[29]) << 8 |
                               uint32_t(ref[30]) << 16 | uint32_t(ref[31]) << 24;
                if (top <= limit) {
                    assert(found < hits.size() && hits[found].nonce == nonce);
                    assert(!memcmp(hits[found++].digest, ref, 32));
                }
            }
            assert(found == hits.size());
        }
        hwsha::hold();
        size_t writesBefore = textWrites;
        std::vector<Hit> hits;
        assert(hwsha::scan(job, 0, 19, UINT32_MAX, hit, &hits) == 0);
        assert(hits.empty() && textWrites == writesBefore);
        hwsha::release(); assert(locks == 0);
    }
    assert(hwsha::selfTest() && hwsha::sparseTextWrites && fallbacks == 0);
    // Model a peripheral which overwrites its TEXT registers during hashing.
    // The real boot self-test must recover the existing hardware mode.
    clobberText = true;
    assert(hwsha::selfTest() && !hwsha::sparseTextWrites && fallbacks == 1);
    assert(hwsha::selfTest() && locks == 0 && fallbacks == 1);
    puts("Resumable SHA: reference digests, nonce wrap, filtering, dirty padding, 20 writes/nonce and boot fallback passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-sha-resume-') as temp:
    cpp, exe = Path(temp) / 'test.cpp', Path(temp) / 'test'
    cpp.write_text(prefix + body + common + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Isrc', str(cpp),
                    'src/core/sha256.cpp', 'src/core/work.cpp', '-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], check=True)
