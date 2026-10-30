// The NerdNOS backend on the host: pin order, the BM1397 start-up sequence,
// chip detection, result verification and the NTC conversion.
#define BOARD_NERDNOS 1
#define BOARD_ASIC 1
#include "../../src/nerdnos.cpp"
#include <cassert>
#include <cmath>
void logf(const char *, ...) {}
static uint8_t power = 100;
static uint32_t generation = 1;
static uint64_t hashes = 0;
static int candidates = 0;
static uint32_t candidateBits = 99;
uint8_t minerPowerPercent() { return power; }
bool minerAsicCurrent(uint32_t g) { return g == generation; }
bool minerAsicWork(AsicWork &) { return false; }
void minerAsicCandidate(AsicWork &, uint32_t, uint32_t bits) { ++candidates; candidateBits = bits; }
void minerAsicHashes(uint64_t n) { hashes += n; }
using namespace nerdnos;
static uint32_t reg(const std::vector<uint8_t> &p) {
    return uint32_t(p[6]) << 24 | uint32_t(p[7]) << 16 | uint32_t(p[8]) << 8 | p[9];
}
static void reset() {
    testPins.clear(); testUartTx.clear(); testUartRx.clear(); testChips = 1; testMillis = 1000;
    power = 100; generation = 1; hashes = 0; candidates = 0; powered = false; maskPower = 6;
    live = {NAN, 0, false, "Starting", -1, 0}; publish();
}
int main() {
    reset(); begin();
    assert(testPins[kReset] == HIGH && testPins[kPower] == LOW);  // held and unpowered until there is work

    assert(initAsic(bm1397::kVersionMask) && powered && live.running);
    assert(testPins[kReset] == LOW && testPins[kPower] == HIGH && testBaud == int(bm1397::kMaxBaud));
    assert(testUartTx.front().size() == 7 && testUartTx.front()[2] == 0x52);  // chip id read first
    bool inactive = false, address = false, ticket = false, divider = false, baud = false;
    int pll = 0;
    for (auto &p : testUartTx) {
        if (p.size() == 7 && p[2] == 0x53) inactive = true;
        if (p.size() == 7 && p[2] == 0x40) address = true;
        if (p.size() != 11 || p[2] != 0x51) continue;
        if (p[5] == 0x14 && reg(p) == 0x000000fc) ticket = true;  // difficulty 64
        if (p[5] == 0x70 && reg(p) == 0x0f0f0f00) divider = true;
        if (p[5] == 0x08) { assert(reg(p) == 0x40780215); ++pll; }  // 300 MHz
        if (p[5] == 0x18 && reg(p) == 0x00006131) baud = true;
    }
    assert(inactive && address && ticket && divider && baud && pll == 2 && live.frequency == 300);

    off("Paused");
    assert(!powered && !live.running && testPins[kReset] == HIGH && testPins[kPower] == LOW);
    reset(); power = 50; assert(initAsic(bm1397::kVersionMask) && std::fabs(live.frequency - 150) < 0.01f);
    reset(); testChips = 0;
    assert(!initAsic(bm1397::kVersionMask) && !powered && testPins[kPower] == LOW && !strcmp(live.state, "BM1397 not detected"));
    reset(); testChips = 2; assert(!initAsic(bm1397::kVersionMask) && !powered);
    reset(); power = 0; assert(!initAsic(bm1397::kVersionMask) && !powered && testPins[kReset] == HIGH);

    // Results, with the genesis block header: its nonce is a real proof of work.
    reset(); assert(initAsic(bm1397::kVersionMask));
    std::vector<uint8_t> genesis;
    assert(work::fromHex("01000000" "0000000000000000000000000000000000000000000000000000000000000000"
                         "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
                         "29ab5f49" "ffff001d" "00000000", genesis));
    Slot &slot = slots[3];
    std::memcpy(slot.work.header, genesis.data(), 80);
    slot.work.generation = 1; slot.versions[0] = 1; slot.count = 1; slot.sent = testMillis; slot.valid = true;
    bm1397::Response r; r.slot = 3; r.midstate = 0; r.nonce = 0x7c2bac1d;
    result(r, testMillis);
    assert(candidates == 1 && candidateBits == 0 && hashes == uint64_t(1) << 38 && live.results == 1);
    result(r, testMillis);  // the same result again
    assert(candidates == 1 && hashes == uint64_t(1) << 38);
    r.nonce = 0x7c2bac1e; result(r, testMillis);  // corrupted: counted as an error, never submitted
    assert(candidates == 1 && windowBad == 1);
    r.midstate = 1; r.nonce = 0x7c2bac1d; result(r, testMillis);  // no such midstate in this job
    assert(candidates == 1 && windowBad == 1);
    generation = 2; r.midstate = 0; slot.seenHead = 0; std::fill(std::begin(slot.seen), std::end(slot.seen), 0);
    result(r, testMillis);  // a new block made the job stale
    assert(candidates == 1);

    // NTC: half the supply is 10 kOhm, 25 C; the rails are a broken sensor.
    testMilliVolts = 1650; assert(std::fabs(temperature() - 25.0f) < 0.05f && std::fabs(status().temperature - 25.0f) < 0.05f);
    testMilliVolts = 0; assert(std::isnan(temperature()));
    testMilliVolts = 3300; assert(std::isnan(temperature()));
    puts("NerdNOS backend: reset before power, BM1397 start-up sequence, chip detection, power limit, verified/duplicate/corrupted/stale results and NTC passed");
}
