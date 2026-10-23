#include "market.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "log.h"
#include "miner.h"
#include "net.h"
#include "settings.h"
#include "tls.h"

namespace {

constexpr const char *kHost = "mempool.space";
constexpr const char *kApi = "https://mempool.space/api";
// Each refresh costs a TLS handshake: ~1.5 s of core 0.
constexpr uint32_t kRefreshMs = 10 * 60 * 1000;
constexpr uint32_t kRetryMs = 30 * 1000;
// A TLS session needs ~45 KB; never starve the miner and the pool socket.
constexpr uint32_t kMinHeapForTls = 70 * 1024;

constexpr uint32_t kPoolStatsMs = 30 * 60 * 1000;
constexpr uint32_t kPoolStatsRetryMs = 60 * 60 * 1000;  // unknown API: try again hourly

MarketData data;
PoolStats pool;
portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

// GET `path` on the TLS connection shared by one refresh and parse the JSON
// body through `filter` (keeps RAM low: large arrays such as the mempool fee
// histogram are skipped). The bodies are a few hundred bytes.
bool fetch(WiFiClientSecure &tls, HTTPClient &http, const char *path, JsonDocument &doc, const JsonDocument &filter) {
    if (!tls.connected() && !tlsConnect(tls, kHost)) {
        logf("[market] %s: cannot connect\n", path);
        return false;
    }

    String url = String(kApi) + path;
    if (!http.begin(tls, url)) {
        tls.stop();
        return false;
    }
    int code = http.GET();
    if (code < 0) {
        // The shared connection was lost (the server closed it, or right
        // after the handshake, seen on mempool.space): one more go on a new one.
        http.end();
        tls.stop();
        if (tlsConnect(tls, kHost) && http.begin(tls, url)) code = http.GET();
    }
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        // Parsed as it arrives when the length is known (the block list is
        // ~30 KB); a chunked body is read whole first.
        DeserializationError err =
            http.getSize() > 0 ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))
                               : deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
        ok = !err;
        if (err) logf("[market] %s: %s\n", path, err.c_str());
    } else {
        logf("[market] %s: HTTP %d\n", path, code);
    }
    http.end();  // keeps the connection open when the server allows it
    return ok;
}

// One refresh of every endpoint. Fields that fail keep their previous value.
bool refresh() {
    if (ESP.getFreeHeap() < kMinHeapForTls) {
        logf("[market] skipped, low memory (%u bytes free)\n", (unsigned)ESP.getFreeHeap());
        return false;
    }
    uint32_t t0 = millis();
    WiFiClientSecure tls;
    HTTPClient http;    // one for the refresh: destroying it closes the connection
    http.setReuse(true);
    http.setTimeout(10000);
    MarketData d = marketGet();
    int ok = 0;

    {
        JsonDocument filter, doc;
        filter[settings.currency.c_str()] = true;
        if (fetch(tls, http, "/v1/prices", doc, filter)) {
            d.price = doc[settings.currency.c_str()] | 0.0;
            ok++;
        }
    }
    {
        JsonDocument filter, doc;
        filter["fastestFee"] = true;
        filter["halfHourFee"] = true;
        filter["hourFee"] = true;
        if (fetch(tls, http, "/v1/fees/recommended", doc, filter)) {
            d.feeFast = doc["fastestFee"] | 0;
            d.feeHalfHour = doc["halfHourFee"] | 0;
            d.feeHour = doc["hourFee"] | 0;
            ok++;
        }
    }
    {
        JsonDocument filter, doc;
        filter["currentHashrate"] = true;
        if (fetch(tls, http, "/v1/mining/hashrate/3d", doc, filter)) {
            d.networkHashrate = doc["currentHashrate"] | 0.0;
            ok++;
        }
    }
    {
        JsonDocument filter, doc;
        filter["progressPercent"] = true;
        filter["difficultyChange"] = true;
        filter["remainingBlocks"] = true;
        if (fetch(tls, http, "/v1/difficulty-adjustment", doc, filter)) {
            d.retargetProgress = doc["progressPercent"] | 0.0;
            d.retargetChange = doc["difficultyChange"] | 0.0;
            d.retargetBlocks = doc["remainingBlocks"] | 0;
            ok++;
        }
    }
    {
        JsonDocument filter, doc;
        filter["count"] = true;
        if (fetch(tls, http, "/mempool", doc, filter)) {
            d.mempoolTx = doc["count"] | 0;
            ok++;
        }
    }
    {
        JsonDocument filter, doc;
        JsonObject b = filter.add<JsonObject>();
        b["height"] = true;
        b["timestamp"] = true;
        b["tx_count"] = true;
        b["extras"]["pool"]["name"] = true;
        if (fetch(tls, http, "/v1/blocks", doc, filter)) {
            d.blockCount = 0;
            for (JsonObject o : doc.as<JsonArray>()) {
                if (d.blockCount == sizeof(d.blocks) / sizeof(d.blocks[0])) break;
                MarketData::Block &k = d.blocks[d.blockCount++];
                k.height = o["height"] | 0;
                k.time = o["timestamp"] | 0;
                k.txs = o["tx_count"] | 0;
                strlcpy(k.pool, o["extras"]["pool"]["name"] | "", sizeof(k.pool));
            }
            ok++;
        }
    }

    if (ok) d.updatedMs = millis();
    portENTER_CRITICAL(&dataMux);
    data = d;
    portEXIT_CRITICAL(&dataMux);
    logf("[market] %d/6 ok in %u ms | 1 BTC = %.0f %s | fees %u/%u/%u | net %.3g H/s | retarget %+.2f%% in %u\n",
         ok, (unsigned)(millis() - t0), d.price, settings.currency.c_str(), d.feeFast, d.feeHalfHour, d.feeHour,
         d.networkHashrate, d.retargetChange, (unsigned)d.retargetBlocks);
    return ok == 6;
}

// "856K" (ckpool hash rates) -> 856000
double parseRate(const char *s) {
    if (!s) return 0;
    char *end;
    double v = strtod(s, &end);
    const char *units = "KMGTPE";
    const char *u = *end ? strchr(units, *end) : nullptr;
    return u ? v * pow(1000.0, u - units + 1) : v;
}

// GET https://host:port/path with its own TLS connection; the body as JSON
// through `filter`. Returns the HTTP code (or a negative error).
int fetchJson(const String &host, uint16_t port, const String &path, JsonDocument &doc, const JsonDocument &filter) {
    WiFiClientSecure tls;
    if (!tlsConnect(tls, host.c_str(), port)) return -1;
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(tls, "https://" + host + ":" + port + path)) return -2;
    int code = http.GET();
    if (code == HTTP_CODE_OK && deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter)))
        code = -3;
    http.end();
    return code;
}

// Refreshes PoolStats for the pool in use; false when it could not.
bool refreshPoolStats() {
    if (ESP.getFreeHeap() < kMinHeapForTls) return false;
    String host = netPoolHost();
    String addr = settings.btcAddress.substring(0, settings.btcAddress.indexOf('.') < 0 ? settings.btcAddress.length()
                                                                                       : settings.btcAddress.indexOf('.'));
    PoolStats p = poolStatsGet();
    JsonDocument filter, doc;
    int code;
    if (host.endsWith("public-pool.io")) {
        filter["bestDifficulty"] = true;
        filter["workersCount"] = true;
        filter["workers"][0]["hashRate"] = true;
        filter["accounting"]["totalAcceptedShares"] = true;
        code = fetchJson("public-pool.io", 40557, "/api/client/" + addr, doc, filter);
        if (code == HTTP_CODE_OK) {
            p.bestEver = atof(doc["bestDifficulty"] | "0");
            p.workers = doc["workersCount"] | 0;
            p.shares = doc["accounting"]["totalAcceptedShares"] | 0.0;
            p.hashrate = 0;
            for (JsonVariant w : doc["workers"].as<JsonArray>()) p.hashrate += w["hashRate"] | 0.0;
        }
    } else {
        // ckpool: https://<pool>/users/<address>
        for (const char *k : {"hashrate1hr", "hashrate5m", "bestever", "shares", "workers"}) filter[k] = true;
        code = fetchJson(host, 443, "/users/" + addr, doc, filter);
        if (code == HTTP_CODE_OK) {
            p.hashrate = parseRate(doc["hashrate1hr"] | doc["hashrate5m"] | "0");
            p.bestEver = doc["bestever"] | 0.0;
            p.shares = doc["shares"] | 0.0;
            p.workers = doc["workers"] | 0;
        }
    }
    p.supported = code == HTTP_CODE_OK || code < 0;  // 404 etc.: no such API (or address unknown there)
    if (code == HTTP_CODE_OK) p.updatedMs = millis();
    portENTER_CRITICAL(&dataMux);
    pool = p;
    portEXIT_CRITICAL(&dataMux);
    if (code == HTTP_CODE_OK)
        logf("[market] pool sees %.3g H/s, best %.4g, %u worker(s)\n", p.hashrate, p.bestEver, (unsigned)p.workers);
    else
        logf("[market] pool stats from %s: HTTP %d\n", host.c_str(), code);
    return code == HTTP_CODE_OK;
}

void marketTask(void *) {
    uint32_t nextMarket = 0, nextPool = 60000;  // pool stats once mining for a minute
    for (;;) {
        uint32_t now = millis();
        if (WiFi.isConnected() && (int32_t)(now - nextMarket) >= 0)
            nextMarket = now + (refresh() ? kRefreshMs : kRetryMs);
        if (WiFi.isConnected() && stats.poolConnected && (int32_t)(now - nextPool) >= 0)
            nextPool = now + (refreshPoolStats() ? kPoolStatsMs : kPoolStatsRetryMs);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

}  // namespace

void marketStart() {
    configTzTime(settings.timezone.c_str(), "pool.ntp.org", "time.cloudflare.com");
    xTaskCreatePinnedToCore(marketTask, "market", 10240, nullptr, 2, nullptr, 0);
}

PoolStats poolStatsGet() {
    portENTER_CRITICAL(&dataMux);
    PoolStats p = pool;
    portEXIT_CRITICAL(&dataMux);
    return p;
}

MarketData marketGet() {
    portENTER_CRITICAL(&dataMux);
    MarketData d = data;
    portEXIT_CRITICAL(&dataMux);
    return d;
}

bool clockNow(struct tm &out) {
    time_t now = time(nullptr);
    if (now < 1700000000) return false;  // not synchronised yet
    localtime_r(&now, &out);
    return true;
}
