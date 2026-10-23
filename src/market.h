// Clock (NTP) and Bitcoin network data from the mempool.space public API:
// price, recommended fees, network hash rate, next difficulty adjustment.
#pragma once

#include <Arduino.h>
#include <time.h>

struct MarketData {
    uint32_t updatedMs = 0;       // millis() of the last successful refresh, 0 = never
    double price = 0;             // 1 BTC in settings.currency, 0 = unknown
    uint16_t feeFast = 0;         // recommended fees, sat/vB
    uint16_t feeHalfHour = 0;
    uint16_t feeHour = 0;
    double networkHashrate = 0;   // H/s
    double retargetChange = 0;    // estimated difficulty change, percent
    double retargetProgress = 0;  // percent of the current epoch done
    uint32_t retargetBlocks = 0;  // blocks until the next adjustment
    uint32_t mempoolTx = 0;       // unconfirmed transactions
};

// What the pool says about this miner's address (public-pool and ckpool
// style APIs: public-pool.io, pool.nerdminers.org, solo.ckpool.org...).
struct PoolStats {
    uint32_t updatedMs = 0;  // millis() of the last successful refresh, 0 = never
    bool supported = true;   // false: this pool has no known stats API
    double hashrate = 0;     // H/s seen by the pool (last hour, or last 10 minutes)
    double bestEver = 0;     // best share difficulty the pool recorded
    double shares = 0;       // accepted shares (ckpool: total difficulty)
    uint32_t workers = 0;
};

// Starts NTP and the background refresh task. Call once WiFi is up.
void marketStart();

// Consistent copy of the latest data.
MarketData marketGet();
PoolStats poolStatsGet();

// Local time; false until NTP has synchronised.
bool clockNow(struct tm &out);
