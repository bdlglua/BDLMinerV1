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
    volatile uint32_t hashes[MAX_MINERS];   // per-miner counters (wrap around)
    uint8_t minerCount;
    const char *engine[MAX_MINERS];        // "sw" / "hw"

    volatile uint32_t sharesFound;   // shares sent to the pool
    volatile uint32_t accepted;
    volatile uint32_t rejected;
    volatile uint32_t blocksFound;
    volatile uint32_t over32bits;    // hashes with >= 32 leading zero bits (diff >= 1)
    double bestDiff;                 // best share difficulty ever (persisted)
    double sessionBestDiff;

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

extern MinerStats stats;

// Publish a new job to the miners. `clean` makes them drop current work.
void minerPublishJob(const work::Job &job, bool clean);
// Stop mining (connection lost): miners idle until the next job.
void minerInvalidate();

// Shares produced by miners, consumed by the network task.
bool minerNextShare(stratum::Share &share, uint32_t waitMs);

// Starts one mining task per available engine/core.
void minerStart();

// Total hashes since boot and an exponentially smoothed
// hash rate in H/s, updated by minerTick() once per second.
uint64_t minerTotalHashes();
double minerHashrate();
// Smoothed hash rate of one engine (stats.engine[i]), H/s.
double minerEngineRate(int i);
void minerTick();

// Copies the hash rate history (H/s, oldest first) and returns its length.
int minerHistory(float *out, int capacity);

// Persist best difficulty, the best shares and lifetime totals across reboots.
void minerLoadStats();
void minerSaveStats();

// The best shares ever found (any difficulty, not only those sent to the
// pool), best first. `time` is a Unix time, 0 when the clock was not set yet.
struct BestShare {
    double difficulty;
    uint32_t time;
};
#define TOP_SHARES 10
int minerTopShares(BestShare *out, int capacity);

// Slows the miners down: Full speed, Half (on battery) or Paused (too hot).
enum class Throttle : uint8_t { Full, Half, Paused };
void minerSetThrottle(Throttle t);
Throttle minerThrottle();
