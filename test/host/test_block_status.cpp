// Production explorer polling with entirely local scripted HTTP responses.
#include <cassert>
#include "../../src/block_status.cpp"
bool candidatePresent = false, memoryReady = true;
BlockCandidate current{};
std::vector<std::string> notifications;
bool minerLastBlockCandidate(BlockCandidate &out) { out = current; return candidatePresent; }
bool tlsMemoryOk() { return memoryReady; }
bool tlsConnectVerified(WiFiClientSecure &tls, const char *host, const char *root, uint16_t, uint8_t) { assert(!strcmp(host,"mempool.space") && strstr(root,"BEGIN CERTIFICATE")); tls.open = true; return true; }
void notify(const char *title, const char *, int, const char *) { notifications.push_back(title); }
int main() {
    blockStatusPoll(); assert(urls.empty());
    candidatePresent = true; current.hash[0] = 1; current.height = 100;
    blockStatusPoll(); assert(urls.empty()); testMillis += kInitialMs;
    replies = {{200,R"({"in_best_chain":true,"height":100})"},{200,"102\n"}};
    blockStatusPoll(); BlockChainStatus status{};
    assert(blockStatusGet(current.hash,status) && status.known && status.inBestChain && status.confirmations == 3 && !status.error);
    assert(urls[0] == "https://mempool.space/api/block/0000000000000000000000000000000000000000000000000000000000000001/status");
    assert(notifications.size() == 1 && replies.empty()); blockStatusPoll(); assert(urls.size() == 2);
    testMillis += kPollMs; replies = {{503,""}}; blockStatusPoll();
    assert(blockStatusGet(current.hash,status) && status.error == 503 && status.checkedMs);
    testMillis += 30000; replies = {{200,R"({"in_best_chain":false})"}}; blockStatusPoll();
    assert(blockStatusGet(current.hash,status) && status.known && !status.inBestChain && status.confirmations == 0);
    assert(notifications.size() == 2 && notifications.back() == "Block chain changed");
    auto failure = [&](Reply reply, int error) {
        testMillis = nextCheck; replies = {reply}; blockStatusPoll();
        assert(blockStatusGet(current.hash,status) && status.error == error);
        assert(replies.empty());
    };
    failure({404,""},404); assert(status.notFound);
    failure({200,R"({"in_best_chain":"true","height":100})"},-4);
    failure({200,R"({"in_best_chain":true,"height":101})"},-4);
    failure({200,std::string(513,'x')},413);
    failure({200,std::string(513,'x'),-1},413);
    failure({200,R"({"in_best_chain":false})",200},-3);
    for (const char *bad : {"", "-1", "+1", "1oops", "4294967296", "10\nbad", "999999999999999999999999"}) {
        uint32_t height; assert(!tipHeight(bad,height));
    }
    testMillis = nextCheck; replies = {{200,R"({"in_best_chain":true,"height":100})"},{200,"99"}};
    blockStatusPoll(); assert(blockStatusGet(current.hash,status) && status.error == -4);
    testMillis = nextCheck; replies = {{200,R"({"in_best_chain":true,"height":100})"},{200,"105"}};
    blockStatusPoll(); assert(blockStatusGet(current.hash,status) && status.confirmations == 6 && !status.error);
    assert(nextCheck == testMillis + 600000);
    testMillis = nextCheck; replies = {{200,R"({"in_best_chain":true,"height":100})"},{200,"106"}};
    bodyHook = [] { ++current.hash[0]; };
    blockStatusPoll(); assert(!blockStatusGet(current.hash,status));
    blockStatusPoll(); testMillis = nextCheck; size_t before = urls.size();
    testOnline = false; blockStatusPoll(); assert(urls.size() == before);
    testOnline = true; memoryReady = false; blockStatusPoll(); assert(urls.size() == before);
    memoryReady = true; replies = {{404,""}}; blockStatusPoll();
    assert(blockStatusGet(current.hash,status) && !status.known && status.notFound);
    testMillis = UINT32_MAX - 5000; ++current.hash[0]; blockStatusPoll(); testMillis += kInitialMs;
    replies = {{200,R"({"in_best_chain":false})"}}; blockStatusPoll();
    assert(blockStatusGet(current.hash,status) && status.known && !status.error);
    puts("Explorer status: idle without proof, delay/backoff, confirmed hash/height, tip and confirmations, reorg, HTTP/malformed/oversized bodies, stale-response isolation, memory/WiFi deferral and timer wrap passed");
}
