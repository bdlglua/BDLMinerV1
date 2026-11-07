import Layout from '../components/Layout';

export const config={unstable_runtimeJS:false};

export default function StatsPage(){
 return <Layout>
<section id="stats">
 <div className="wrap">
  <h1 data-t="statsTitle">BDLMiner right now</h1>
  <p className="lead" data-t="statsLead">Live totals from anonymous hashrate reports: firmware version, board, hashrate, cryptocurrency and optional public pool group. No Bitcoin address, worker or private endpoint.</p>
  <div className="tiles" aria-live="polite">
   <div className="tile"><div className="label" data-t="tHashrate">Total hashrate</div><div className="value accent" id="st-hashrate">–</div></div>
   <div className="tile"><div className="label" data-t="tMiners">Miners</div><div className="value" id="st-miners">–</div></div>
   <div className="tile"><div className="label" data-t="tAverage">Average per miner</div><div className="value" id="st-average">–</div></div>
   <div className="tile"><div className="label" data-t="tRecord">Best hour</div><div className="value" id="st-record">–</div></div>
  </div>
  <div className="fleet-head">
   <h3 data-t="devicesTitle">Devices</h3>
   <span className="legend"><span className="dot on" aria-hidden="true"></span><span><b id="st-on">–</b> <span data-t="online">online</span></span></span>
   <span className="legend"><span className="dot off" aria-hidden="true"></span><span><b id="st-off">–</b> <span data-t="offline">offline</span></span></span>
  </div>
  <ul className="fleet" id="st-fleet"></ul>
  <button type="button" className="fleet-more" id="st-more" aria-expanded="false" aria-controls="st-fleet" hidden></button>
  <div className="card">
   <div className="chart-head">
    <span className="live"><span className="dot on" aria-hidden="true"></span><span data-t="live">Live</span></span>
    <span className="s" id="st-updated" aria-live="off"></span>
    <div className="range" role="group" aria-label="Period" data-al="period">
     <button type="button" data-range="day" aria-pressed="true">24 h</button>
     <button type="button" data-range="week" aria-pressed="false" data-t="week">7 days</button>
    </div>
   </div>
   <svg className="chart" id="st-chart" viewBox="0 0 960 200" role="img" aria-label="Total hashrate, last 24 hours"></svg>
  </div>
  <div className="split">
   <div className="card"><h3 data-t="byBoard">By board</h3><table className="by" id="st-boards"></table></div>
   <div className="card"><h3 data-t="byVersion">By version</h3><table className="by" id="st-versions"></table></div>
  </div>
  <div className="card coin-breakdown"><h3 data-t="coinsTitle">By cryptocurrency</h3><table className="by" id="st-coins"></table></div>
  <div className="card pool-breakdown"><h3 data-t="byPool">By pool</h3><table className="by" id="st-pools"></table></div>
  <p className="s" id="st-status" data-h="statsMore">Raw data: <a href="https://bdlminer-stats.bdlminer-bot.workers.dev/api/summary">JSON</a> · <a href="https://bdlminer-stats.bdlminer-bot.workers.dev/">statistics page</a>.</p>
 </div>
</section>
 </Layout>;
}
