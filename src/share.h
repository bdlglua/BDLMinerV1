// Anonymous hashrate sharing (settings.shareHashrate, on by default): every 5
// minutes, the firmware version, the board and the hashrate are posted to the
// BDLMiner statistics Worker, which adds up the reports of every miner.
// Nothing else is sent: no address, worker, pool, name or identifier.
#pragma once

#include <stddef.h>

#ifndef STATS_URL
#define STATS_URL "https://bdlminer-stats.bdlminer-bot.workers.dev/report"
#endif

// Sends the report when it is due. Called every few seconds by the market
// task, like the other HTTPS requests.
void sharePoll();

// The JSON body sent: {"v":"1.9.2","board":"m5stickc-plus2","hashrate":1.02e6,"every":300}
// with the hashrate rounded to 3 significant digits and "every" the seconds
// until the next report. Length written, 0 if
// `size` is too small.
size_t shareBody(char *out, size_t size, double hashrate);
