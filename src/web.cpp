#include "web.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "board.h"
#include "core/address.h"
#include "market.h"
#include "miner.h"
#include "net.h"
#include "settings.h"

namespace {

WebServer server(80);
String hostName;

const char kPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BDLMiner V1</title>
<style>
:root{--bg:#0b0d12;--panel:#161b26;--border:#2b3344;--label:#8c96aa;--fg:#eceef2;--accent:#f7931a;--ok:#38d07a;--bad:#e5484d}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif;padding:16px}
header{display:flex;align-items:center;gap:10px;max-width:960px;margin:0 auto 16px}
.coin{width:30px;height:30px;border-radius:50%;background:var(--accent);color:#fff;font-weight:900;display:grid;place-items:center;transform:rotate(14deg)}
h1{font-size:20px;margin:0}h1 b{color:var(--accent)}#st{margin-left:auto;font-weight:700;font-size:13px}
main{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:10px;max-width:960px;margin:0 auto}
.t{background:var(--panel);border:1px solid var(--border);border-radius:8px;padding:10px 12px;min-width:0}
.t.big{grid-column:span 2;border-top:2px solid var(--accent)}
.l{color:var(--label);font-size:11px;font-weight:700;letter-spacing:.06em;text-transform:uppercase}
.v{font-size:20px;font-weight:700;overflow-wrap:anywhere;font-variant-numeric:tabular-nums}
.big .v{font-size:38px;color:var(--accent)}.s{color:var(--label);font-size:12px}
footer{max-width:960px;margin:16px auto 0;color:var(--label);font-size:12px}
@media(max-width:460px){.t.big{grid-column:span 1}}
</style></head><body>
<header><div class="coin">B</div><h1>BDLMiner <b>V1</b></h1><span id="st">…</span></header>
<main id="m"></main>
<footer id="f"></footer>
<script>
const SI=['','k','M','G','T','P','E','Z','Y'];
function si(v,u=''){if(!v)return'-';let i=0;while(Math.abs(v)>=1000&&i<SI.length-1){v/=1000;i++}return v.toFixed(v<10?2:v<100?1:0)+' '+SI[i]+u}
function diff(d){return!d?'-':d<1?d.toFixed(5):si(d)}
function dur(s){const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;return(d?d+'d ':'')+h+'h '+m+'m'}
function years(y){const W=[[1e12,'trillion'],[1e9,'billion'],[1e6,'million'],[1e3,'thousand']];for(const[k,w]of W)if(y>=k)return(y/k).toFixed(1)+' '+w+' years';return y>=1?y.toFixed(1)+' years':(y*365).toFixed(1)+' days'}
function chart(h){const v=h.hashrate,t=tile('Hashrate history',undefined,v.length?'5-min averages, last '+dur(v.length*h.interval_s)+' · avg '+si(v.reduce((a,b)=>a+b,0)/v.length,'H/s')+(v.length>1?' · range '+si(Math.min(...v))+'–'+si(Math.max(...v),'H/s'):''):'first point after '+h.interval_s/60+' min of mining',true);
 if(v.length>1){const N='http://www.w3.org/2000/svg',W=300,H=60,lo=Math.min(...v),hi=Math.max(...v),k=(H-4)/((hi-lo)||hi||1),s=document.createElementNS(N,'svg'),p=document.createElementNS(N,'polyline');
  s.setAttribute('viewBox','0 0 '+W+' '+H);s.setAttribute('preserveAspectRatio','none');s.style.cssText='display:block;width:100%;height:60px;margin:6px 0';
  p.setAttribute('points',v.map((y,i)=>(i*W/(v.length-1)).toFixed(1)+','+(H-2-(y-lo)*k).toFixed(1)).join(' '));
  p.setAttribute('vector-effect','non-scaling-stroke');p.style.cssText='fill:none;stroke:var(--accent);stroke-width:2';
  s.appendChild(p);t.insertBefore(s,t.lastChild)}return t}
function tile(label,value,sub,big){const t=document.createElement('div');t.className='t'+(big?' big':'');
 for(const[c,x]of[['l',label],['v',value],['s',sub]])if(x!==undefined){const e=document.createElement('div');e.className=c;e.textContent=x;t.appendChild(e)}return t}
async function tick(){
 try{const r=await fetch('/api/stats');const j=await r.json();const m=j.market;
  const st=document.getElementById('st');st.textContent=j.pool.connected?'ONLINE':'OFFLINE';st.style.color=j.pool.connected?'var(--ok)':'var(--bad)';
  const odds=j.hashrate&&j.network.difficulty?j.network.difficulty*4294967296/j.hashrate/31557600:0;
  const T=[tile('Hashrate',si(j.hashrate,'H/s'),j.engines.join(' + ')+' engines',true),
   tile('Shares',j.shares.accepted+(j.shares.rejected?' / '+j.shares.rejected+' rejected':''),'pool diff '+diff(j.pool.difficulty)),
   tile('Best difficulty',diff(j.best.session),'best ever '+diff(j.best.ever)),
   tile('Blocks found',j.blocks,j.over32bits+' hashes ≥ diff 1'),
   tile('Total hashes',si(j.total_hashes),'uptime '+dur(j.uptime_s)),
   tile('Block height',j.network.height||'-','net diff '+diff(j.network.difficulty)),
   tile('Solo odds',odds?'1 block / '+years(odds):'-','at your hash rate'),
   tile('Lifetime',si(j.lifetime.hashes)+' hashes',j.lifetime.shares+' shares · '+j.lifetime.blocks+' blocks'),
   chart(j.history)];
  if(m.updated_s>=0){T.push(tile('BTC price',m.price?Math.round(m.price).toLocaleString()+' '+m.currency:'-',m.price?Math.round(1e8/m.price).toLocaleString()+' sats / '+m.currency:'',true),
   tile('Fees sat/vB',m.fees.join(' / '),'fast / 30 min / 1 h'),
   tile('Network hashrate',si(m.hashrate,'H/s'),m.mempool_tx.toLocaleString()+' tx in mempool'),
   tile('Next retarget',(m.retarget.change>=0?'+':'')+m.retarget.change.toFixed(2)+' %','in '+m.retarget.blocks+' blocks ('+m.retarget.progress.toFixed(0)+' % done)'))}
  T.push(tile(j.pool.backup?'Pool (backup)':'Pool',j.pool.host+':'+j.pool.port,j.pool.status),
   tile('Payout address',j.address,j.address_type==='invalid'?'⚠ not a valid Bitcoin address':j.address_type),
   tile('Device',j.board,'v'+j.firmware+' · '+j.temp_c+' °C · '+Math.round(j.heap/1024)+' KB free'),
   tile('WiFi',j.ip,j.rssi+' dBm'));
  document.getElementById('m').replaceChildren(...T);
  document.getElementById('f').textContent='Updated '+new Date().toLocaleTimeString()+(m.updated_s>=0?' · market data '+Math.round(m.updated_s/60)+' min old (mempool.space)':'');
 }catch(e){document.getElementById('st').textContent='NO CONNECTION';}
}
tick();setInterval(tick,3000);
</script></body></html>
)HTML";

void handleStats() {
    JsonDocument j;
    j["board"] = board::modelName();
    j["firmware"] = FW_VERSION;
    j["uptime_s"] = (millis() - stats.startMillis) / 1000;
    j["hashrate"] = minerHashrate();
    j["total_hashes"] = (double)minerTotalHashes();
    JsonArray engines = j["engines"].to<JsonArray>();
    for (int i = 0; i < stats.minerCount; i++) engines.add(stats.engine[i]);
    j["shares"]["accepted"] = stats.accepted;
    j["shares"]["rejected"] = stats.rejected;
    j["best"]["session"] = stats.sessionBestDiff;
    j["best"]["ever"] = stats.bestDiff;
    j["blocks"] = stats.blocksFound;
    j["over32bits"] = stats.over32bits;
    j["pool"]["host"] = netPoolHost();
    j["pool"]["port"] = netPoolPort();
    j["pool"]["backup"] = netOnBackup();
    j["pool"]["connected"] = stats.poolConnected;
    j["pool"]["status"] = netStatus();
    j["pool"]["difficulty"] = stats.poolDiff;
    j["network"]["height"] = stats.blockHeight;
    j["network"]["difficulty"] = stats.networkDiff;
    j["address"] = settings.btcAddress;
    j["address_type"] = address::typeName(address::parse(settings.btcAddress.c_str()).type);
    j["lifetime"]["hashes"] = (double)stats.lifetimeHashes;
    j["lifetime"]["shares"] = stats.lifetimeShares;
    j["lifetime"]["blocks"] = stats.lifetimeBlocks;
    static float hist[HISTORY_LEN];
    int n = minerHistory(hist, HISTORY_LEN);
    j["history"]["interval_s"] = HISTORY_INTERVAL_S;
    JsonArray hr = j["history"]["hashrate"].to<JsonArray>();
    for (int i = 0; i < n; i++) hr.add((long)hist[i]);
    j["heap"] = ESP.getFreeHeap();
    j["temp_c"] = (int)temperatureRead();
    j["ip"] = WiFi.localIP().toString();
    j["rssi"] = WiFi.RSSI();

    MarketData m = marketGet();
    JsonObject mk = j["market"].to<JsonObject>();
    mk["updated_s"] = m.updatedMs ? (long)((millis() - m.updatedMs) / 1000) : -1;
    mk["currency"] = settings.currency;
    mk["price"] = m.price;
    JsonArray fees = mk["fees"].to<JsonArray>();
    fees.add(m.feeFast);
    fees.add(m.feeHalfHour);
    fees.add(m.feeHour);
    mk["hashrate"] = m.networkHashrate;
    mk["mempool_tx"] = m.mempoolTx;
    mk["retarget"]["change"] = m.retargetChange;
    mk["retarget"]["progress"] = m.retargetProgress;
    mk["retarget"]["blocks"] = m.retargetBlocks;

    String body;
    serializeJson(j, body);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", body);
}

// Prometheus text format, for Grafana / Home Assistant scrapers.
void handleMetrics() {
    String out;
    out.reserve(1536);
    auto metric = [&out](const char *name, const char *type, double value, const char *labels = "") {
        char line[160];
        snprintf(line, sizeof(line), "# TYPE %s %s\n%s%s %.17g\n", name, type, name, labels, value);
        out += line;
    };
    metric("bdlminer_hashrate_hps", "gauge", minerHashrate());
    metric("bdlminer_hashes_total", "counter", (double)minerTotalHashes());
    metric("bdlminer_shares_accepted_total", "counter", stats.accepted);
    metric("bdlminer_shares_rejected_total", "counter", stats.rejected);
    metric("bdlminer_blocks_found_total", "counter", stats.blocksFound);
    metric("bdlminer_best_difficulty", "gauge", stats.sessionBestDiff);
    metric("bdlminer_best_difficulty_ever", "gauge", stats.bestDiff);
    metric("bdlminer_lifetime_hashes_total", "counter", (double)stats.lifetimeHashes);
    metric("bdlminer_lifetime_shares_total", "counter", stats.lifetimeShares);
    metric("bdlminer_pool_connected", "gauge", stats.poolConnected ? 1 : 0);
    metric("bdlminer_pool_backup", "gauge", netOnBackup() ? 1 : 0);
    metric("bdlminer_pool_difficulty", "gauge", stats.poolDiff);
    metric("bdlminer_block_height", "gauge", stats.blockHeight);
    metric("bdlminer_network_difficulty", "gauge", stats.networkDiff);
    metric("bdlminer_uptime_seconds", "gauge", (millis() - stats.startMillis) / 1000);
    metric("bdlminer_temperature_celsius", "gauge", temperatureRead());
    metric("bdlminer_wifi_rssi_dbm", "gauge", WiFi.RSSI());
    metric("bdlminer_free_heap_bytes", "gauge", ESP.getFreeHeap());
    char labels[96];
    snprintf(labels, sizeof(labels), "{board=\"%s\",firmware=\"%s\"}", board::modelName(), FW_VERSION);
    metric("bdlminer_info", "gauge", 1, labels);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain; version=0.0.4", out);
}

}  // namespace

void webStart() {
    char name[24];
    snprintf(name, sizeof(name), "bdlminer-%04x", (unsigned)((ESP.getEfuseMac() >> 32) & 0xFFFF));
    hostName = name;
    if (MDNS.begin(name)) MDNS.addService("http", "tcp", 80);

    server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", kPage); });
    server.on("/api/stats", HTTP_GET, handleStats);
    server.on("/metrics", HTTP_GET, handleMetrics);
    server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
    server.begin();
    Serial.printf("[web] dashboard: http://%s/ or http://%s.local/\n", WiFi.localIP().toString().c_str(), name);
}

void webHandle() { server.handleClient(); }

const String &webHostName() { return hostName; }
