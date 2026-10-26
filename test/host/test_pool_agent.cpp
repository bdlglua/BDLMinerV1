#include <cassert>
#include <cstdio>
#include <cstring>
#include "core/pool_agent.h"
#include "core/stratum.h"

int main() {
    constexpr const char *native = "BDLMinerV1/test";
    stratum::PoolAgent main, backup;
    for (const char *host : {"pool.nerdminers.org", "POOL.NERDMINERS.ORG.",
                             "pool.nerdminer.io", "pool.bitronics.store",
                             "pool.bitronics.com", "pool.solomining.de"}) {
        main.endpoint(host, 3333);
        assert(!strcmp(main.select(native), native));
        assert(!strcmp(main.select(native, false), native));
        assert(!strcmp(main.select(native, false, "CustomMiner"), "CustomMiner"));
    }
    main.endpoint("pool.nerdminers.org.example", 3333);
    assert(!strcmp(main.select(native), native));  // exact hosts, not a substring
    assert(!main.refused("Invalid username"));
    assert(!main.refused("Pool is full"));
    assert(!main.refused(nullptr));

    stratum::Protocol protocol;
    assert(protocol.subscribe(main.select(native)).find(native) != std::string::npos);
    assert(protocol.handleLine(R"({"id":1,"result":"Only allowed USER AGENTS may subscribe","error":null})") ==
           stratum::Event::SubscribeFailed);
    assert(main.refused(protocol.lastError().c_str()));
    assert(!main.refused(protocol.lastError().c_str()));  // no retry loop
    protocol.reset();
    main.endpoint("pool.nerdminers.org.example", 3333); // reconnect keeps learned choice
    assert(protocol.subscribe(main.select(native)).find("NerdMinerV2") != std::string::npos);
    assert(protocol.handleLine(R"({"id":1,"result":[[],"aabb",4],"error":null})") == stratum::Event::Subscribed);
    assert(protocol.handleLine(R"({"id":2,"result":true,"error":null})") == stratum::Event::Authorized);

    backup.endpoint("backup.example", 3333);
    assert(!strcmp(backup.select(native), native)); // main refusal cannot leak into backup
    assert(!backup.refused("Invalid worker"));
    main.endpoint("pool.nerdminers.org.example", 3334); // a different service resets learning
    assert(!strcmp(main.select(native), native));
    main.endpoint("pool.nerdminers.org", 3333);
    assert(!strcmp(main.select(native), native));
    assert(main.refused("Only allowed user agents may subscribe"));
    main.endpoint("POOL.NERDMINERS.ORG.", 3333);
    assert(!strcmp(main.select(native), "NerdMinerV2")); // learned fallback survives reconnect
    assert(!strcmp(main.select(native, false, native), native)); // manual never uses learned fallback
    assert(!strcmp(main.select(native, false, "NerdMinerV2"), "NerdMinerV2")); // explicit manual choice retained
    main.endpoint("other.example", 3333);
    assert(!strcmp(main.select(native), native));
    assert(!strcmp(backup.select(native), native));
    puts("Pool agent: native default on every endpoint, explicit-refusal fallback, manual identity, retry bounds and backup isolation passed");
}
