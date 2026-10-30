// Mining tasks (software SHA on every core, hardware SHA where supported),
// shared job board, statistics and the share queue.
#pragma once

#include <Arduino.h>

#include "core/stratum.h"
#include "core/work.h"

#define MAX_MINERS 3
// Hash rate history: one average every 5 minutes, last 24 hours.
#define HISTORY_INTERVAL_S 300
#define HISTORY_LEN 288

struct MinerStats {
#ifdef BOARD_ASIC
    volatile uint64_t hashes[MAX_MINERS];
#else
    volatile uint32_t hashes[MAX_MINERS];
#endif // per-engine counters (unsigned wrap)
    uint8_t minerCount;
    const char *engine[MAX_MINERS];        // "sw" / "hw"

    volatile uint32_t sharesFound;   // shares sent to the pool
    volatile uint32_t accepted;
    volatile uint32_t rejected;
    double acceptedWork;             // sum of the pool difficulty of accepted shares, this boot
    volatile uint32_t blocksFound;
    volatile uint32_t over32bits;    // hashes with >= 32 leading zero bits (diff >= 1)
    volatile uint32_t over64bits;    // hashes with >= 64 leading zero bits, this boot
    double bestDiff;                 // best share difficulty ever (persisted)
    double sessionBestDiff;
    double periodBestDiff;           // since minerTakePeriodBest(): the daily summary

    // Totals since the first boot (persisted), this session included.
    uint64_t lifetimeHashes;
    uint32_t lifetimeShares;
    uint32_t lifetimeBlocks;
    uint32_t lifetimeSeconds;  // time spent running

    // Job info for the UI.
    volatile uint32_t blockHeight;
    double networkDiff;
    double poolDiff;
    bool poolConnected;
    uint32_t startMillis;
};

// Coherent copy for UI/network consumers; shared storage stays in miner.cpp.
MinerStats minerStats();
void minerPoolConnected(bool connected);
// The last shares sent to the pool and its answers, for the dashboard. The
// pool answers in order, so each result goes to the oldest pending share.
struct ShareEntry {
    uint32_t time;      // Unix time, 0 when the clock was not set
    float difficulty;   // of the hash
    uint8_t state;      // SharePending, ShareAccepted, ShareRejected or ShareUnanswered
    bool backup;        // sent to the backup pool
};
enum : uint8_t { SharePending, ShareAccepted, ShareRejected, ShareUnanswered };
void minerShareSent(double difficulty, bool backup);
int minerShareLog(ShareEntry *out, int capacity);  // newest first
struct PoolUptime {
    uint32_t connectedMs = 0;  // since boot
    uint32_t sessions = 0;     // connections that reached mining
};
PoolUptime minerPoolUptime();
void minerPoolDifficulty(double difficulty);
void minerShareResult(bool accepted);
uint32_t minerUptimeSeconds();

// Publish a new job to the miners. `clean` makes them drop current work.
void minerPublishJob(const work::Job &job, bool clean);
// Stop mining (connection lost): miners idle until the next job.
void minerInvalidate();

// Shares produced by miners, consumed by the network task.
bool minerNextShare(stratum::Share &share, uint32_t waitMs);

// Starts one mining task per available engine/core.
void minerStart();

// Total hashes since boot and the hash rate in H/s (average of the last
// minute), updated by minerTick() once per second.
uint64_t minerTotalHashes();
double minerHashrate();
// Hash rate over the last 10 seconds, for the screen: the 60-second average
// barely moves and looks frozen.
double minerHashrateLive();
// Smoothed hash rate of one engine (stats.engine[i]), H/s.
double minerEngineRate(int i);
void minerTick();

// Copies the hash rate history (H/s, oldest first) and returns its length.
int minerHistory(float *out, int capacity);
struct HistoryPoint {
    float hashrate;
    int16_t temperatureDeciC;  // INT16_MIN when no usable sample
    uint8_t powerPercent;
    uint8_t coveragePercent;
};
int minerHistoryPoints(HistoryPoint *out, int capacity, uint32_t *revision = nullptr);
uint32_t minerHistoryRevision();
// The best share difficulty since the previous call (then starts a new period).
double minerTakePeriodBest();
void minerObserve(float temperature, uint8_t effectivePower);
struct MinerProfile {
    uint64_t hashingUs = 0, restMs = 0, waitingMs = 0, pausedMs = 0;
};
void minerProfiles(MinerProfile *out, int capacity);

// Persist best difficulty, the best shares and lifetime totals across reboots
// (two NVS slots written in turn, each with a sequence number and a CRC: a
// power cut during a save loses at most the previous save). Saving does
// nothing before minerLoadStats() has read the saved stats back.
void minerLoadStats();
// `periodic`: the 10-minute save, which writes the hashrate history only every
// 30 minutes; any other call (before a restart) writes it too.
bool minerSaveStats(bool periodic = false);
// Merges the history saved before the last restart once the clock is set; call
// once a second from the main loop.
void minerHistoryPoll();

// The best shares ever found (any difficulty, not only those sent to the
// pool), best first. `time` is a Unix time, 0 when the clock was not set yet.
struct BestShare {
    double difficulty;
    uint32_t time;
};
#define TOP_SHARES 10
int minerTopShares(BestShare *out, int capacity);

// Your best share while mining on each recent block height, newest first
// (the first one is the block being mined now).
struct BlockBest {
    uint32_t height;
    double best;
};
#define BLOCK_BESTS 16
int minerBlockBests(BlockBest *out, int capacity);

// Lifetime statistics, for a backup.
struct StatsSnapshot {
    double bestDiff;
    uint64_t hashes;
    uint32_t shares, blocks, seconds;
    BestShare top[TOP_SHARES];
};
void minerStatsSnapshot(StatsSnapshot &out);
// Saves `s` as the lifetime statistics, used from the next start on: restart
// after it (nothing is saved meanwhile, so it is not overwritten).
bool minerStatsRestore(const StatsSnapshot &s);

// Slows the miners down: Full speed, Half (on battery) or Paused (too hot).
enum class Throttle : uint8_t { Full, Half, Paused, Reduced };
// Refresh the safety lease after sampling temperature, at least once a second.
void minerSetPower(uint8_t percent, bool batterySaver = false);
uint8_t minerPowerPercent();
Throttle minerThrottle();

#ifdef BOARD_ASIC
struct AsicWork : work::Work { uint32_t generation = 0, height = 0; };
bool minerAsicWork(AsicWork &out);
bool minerAsicCurrent(uint32_t generation);
void minerAsicCandidate(AsicWork &work, uint32_t nonce, uint32_t versionBits);
void minerAsicHashes(uint64_t hashes);
#endif
