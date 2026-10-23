// Mining tasks (software SHA on every core, hardware SHA where supported),
// shared job board, statistics and the share queue.
#pragma once

#include <Arduino.h>

#include "core/stratum.h"
#include "core/work.h"

#define MAX_MINERS 3

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
void minerTick();

// Persist best difficulty across reboots.
void minerLoadBest();
void minerSaveBest();
