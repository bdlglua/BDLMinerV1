// Explorer observations, independent of pool share ACKs and payouts.
#pragma once
#include <stdint.h>
struct BlockChainStatus {
    uint8_t hash[32]{}; // digest order, matched to the latest local candidate
    bool known = false, inBestChain = false, notFound = false;
    uint32_t height = 0, confirmations = 0, checkedMs = 0;
    int error = 0;
};
// Called only by the background market task. No candidate => no request.
void blockStatusPoll();
// False if the retained status belongs to another candidate. `known` stays
// false until a valid observation; errors make the current result uncertain.
bool blockStatusGet(const uint8_t hash[32], BlockChainStatus &out);
