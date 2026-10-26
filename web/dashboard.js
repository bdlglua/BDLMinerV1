let cachedHistory={hashrate:[],interval_s:300},historyRevision=null,historyRetryAt=0,lastUptime=-1,lastBootId=null,latestStats=null;
const SI=['','k','M','G','T','P','E','Z','Y'];
function si(v,u=''){if(v==null||!Number.isFinite(v))return'-';if(v===0)return'0'+(u?' '+u:'');let i=0;while(Math.abs(v)>=1000&&i<SI.length-1){v/=1000;i++}return v.toFixed(v<10?2:v<100?1:0)+' '+SI[i]+u}
function diff(d){return!d?'-':d<1?d.toFixed(5):si(d)}
function dur(s){if(!Number.isFinite(s))return'-';s=Math.max(0,s);if(s<60)return Math.round(s)+'s';const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;return(d?d+(LANG=='fr'?'j ':'d '):'')+(d||h?h+'h ':'')+m+'m'}
function odds(hr,nd,s){if(!hr||!nd)return'-';const p=-Math.expm1(-hr*s/(nd*4294967296));return(LANG=='fr'?'1 sur ':'1 in ')+si(1/p).replace(' ','')}
function chart(h={}){const v=Array.isArray(h.hashrate)?h.hashrate.filter(Number.isFinite):[],t0=tile(t('Hashrate history'),undefined,v.length?t('5-min averages, last')+' '+dur(v.length*h.interval_s)+' · '+t('avg')+' '+si(v.reduce((a,b)=>a+b,0)/v.length,'H/s')+(v.length>1?' · '+t('range')+' '+si(Math.min(...v))+'–'+si(Math.max(...v),'H/s'):''):t('first point after')+' '+h.interval_s/60+' '+t('min of mining'),true);
 t0.id='hashrate-history';
 if(v.length>1){const N='http://www.w3.org/2000/svg',W=600,H=140,lo=Math.min(...v),hi=Math.max(...v),k=(H-4)/((hi-lo)||hi||1),s=document.createElementNS(N,'svg'),p=document.createElementNS(N,'polyline');
  s.setAttribute('viewBox','0 0 '+W+' '+H);s.setAttribute('preserveAspectRatio','none');s.style.cssText='display:block;width:100%;height:140px;margin:6px 0';
  p.setAttribute('points',v.map((y,i)=>(i*W/(v.length-1)).toFixed(1)+','+(H-2-(y-lo)*k).toFixed(1)).join(' '));
  p.setAttribute('vector-effect','non-scaling-stroke');p.style.cssText='fill:none;stroke:var(--accent);stroke-width:2';
  s.appendChild(p);t0.insertBefore(s,t0.lastChild)}return t0}
function tile(label,value,sub,big){const e=document.createElement('div');e.className='t'+(big?' big':'');
 if(label===t('Hashrate'))e.classList.add('hero');
 if(label===t('Hashrate history'))e.classList.add('history');
 for(const [group,names]of Object.entries({pool:['Pool','Pool (backup)','Payout address','Seen by the pool'],market:['BTC price','Fees sat/vB','Network hashrate','Next retarget','Recent blocks'],device:['Device','WiFi','Temperature','Memory','Effective power']}))if(names.some(name=>label===t(name)))e.dataset.group=group;
 for(const[c,x]of[['l',label],['v',value],['s',sub]])if(x!==undefined){const d=document.createElement('div');d.className=c;d.textContent=x;e.appendChild(d)}return e}

let installPending=false,installRequestId=0,installError='',checkPending=false;
function upd(u){
 if(!u)return;
 if(installPending&&(u.request_id||0)!==installRequestId)installPending=false;
 const status=u.mode===0?'Updates disabled in settings.':u.checking?'Checking…':u.checked?'Latest release checked.':'Waiting for the first check.';
 const e=tile(t(u.available?'Update available':'GitHub updates'),u.version||'-',[u.error||u.notes||installError||t(status),u.retry_in_s?t('Next retry in')+' '+dur(u.retry_in_s):''].filter(Boolean).join('\n'),true);
 if(u.available&&u.supported!==false){
 const b=document.createElement('button');b.className='gh-install';
 b.type='button';b.disabled=installPending||u.installing||u.checking;b.textContent=t(u.checking?'Checking…':b.disabled?'Installing…':u.retry_in_s?'Retry now':'Install');
 b.onclick=async()=>{
  if(installPending||u.installing||u.checking)return;
  installRequestId=u.request_id||0;installPending=true;installError='';b.disabled=true;b.textContent=t('Installing…');
  try{await request('/api/github-update',{method:'POST'},'text');installError=t('Installation requested.')}
  catch(err){installPending=false;installError=errorText(err);b.disabled=false;b.textContent=t('Install')}
  e.querySelector('.s').textContent=installError;
 };
 e.appendChild(b);
 }
 if(u.available&&u.supported===false){const link=document.createElement('a');link.className='button secondary';link.textContent=t('Download via USB');if(u.release_url&&/^https:\/\/github\.com\//.test(u.release_url)){link.href=u.release_url;link.target='_blank';link.rel='noopener noreferrer';e.appendChild(link)}}
 const c=document.createElement('button');c.className='gh-check';c.type='button';
 c.disabled=checkPending||installPending||u.checking||u.installing||u.mode===0;
 c.textContent=t(checkPending||u.checking?'Checking…':'Check now');
 c.onclick=async()=>{
  if(c.disabled)return;
  checkPending=true;c.disabled=true;c.textContent=t('Checking…');
  try{await request('/api/github-check',{method:'POST'},'text');installError=t('Check requested.')}
  catch(err){installError=errorText(err);c.disabled=false;c.textContent=t('Check now')}
  finally{checkPending=false}
  e.querySelector('.s').textContent=installError;
 };
 e.appendChild(c);return e;
}
function peerHosts(list,ip){
 const seen=new Set(),hosts=[];
 if(!Array.isArray(list))return hosts;
 for(const entry of list||[]){
  try{
   const u=new URL(entry.includes('://')?entry:'http://'+entry);
   if(!['http:','https:'].includes(u.protocol)||u.username||u.password)continue;
   if(u.origin===location.origin||(u.hostname===ip&&(u.port||'80')===(location.port||'80'))||seen.has(u.origin))continue;
   seen.add(u.origin);hosts.push(u);
  }catch(e){}
 }
 return hosts;
}
async function peer(u){
 try{
  const p=await request(u.origin+'/api/stats?history=0',{timeout:2500});
  if(!p||!Number.isFinite(p.hashrate)||p.hashrate<0||!p.pool||typeof p.pool.connected!=='boolean'||!p.shares||!Number.isFinite(p.shares.accepted)||!p.best||!Number.isFinite(p.best.ever))throw Error('Invalid peer');
  return p;
 }catch(e){return null}
}
async function tick(){
 try{const j=await request('/api/stats?history=0');const m=j.market,pv=j.pool_view;
  if(!m||!pv||!j.pool||!Array.isArray(j.engines))throw Error(t('Invalid response from the miner.'));
  if(j.uptime_s<lastUptime||(lastBootId&&j.boot_id&&j.boot_id!==lastBootId)){historyRevision=null;cachedHistory={hashrate:[],interval_s:300};historyRetryAt=0}
  lastUptime=j.uptime_s;lastBootId=j.boot_id||null;latestStats=j;
  if(j.history)cachedHistory=j.history;
  sampleBenchmark(j);
  document.getElementById('start-benchmark').disabled=benchmarkBusy||!!benchmark;
  document.documentElement.style.setProperty('--accent',j.accent);
  const st=document.getElementById('st');st.textContent=t(j.pool.connected?'ONLINE':'OFFLINE');st.style.color=j.pool.connected?'var(--ok)':'var(--bad)';
  const state=j.throttle=='paused'?'⚠ '+t('mining paused'):j.throttle=='half'?t('battery saver: half speed'):j.throttle=='reduced'?t('power limited'):'';
  const T=[tile(t('Hashrate'),si(j.hashrate,'H/s'),t('1-minute average')+(Number.isFinite(j.hashrate_live)?' · '+t('Live (10 s)')+': '+si(j.hashrate_live,'H/s'):'')+'\n'+j.engines.map(e=>e.name+' '+si(e.rate,'H/s')).join(' + ')+(state?'\n'+state+' · '+j.power_percent+' %':''),true),
   tile(t('Shares'),j.shares.accepted+(j.shares.rejected?' / '+j.shares.rejected+' '+t('rejected'):''),t('pool diff')+' '+diff(j.pool.difficulty)+(j.pool.pending_shares?' · '+j.pool.pending_shares+' '+t('awaiting response'):'')),
   tile(t('Best difficulty'),diff(j.best.session),t('best ever')+' '+diff(j.best.ever)),
   tile(t('32-bit shares'),j.over32bits,t('1 every')+' '+(j.hashrate?dur(4294967296/j.hashrate):'-')+' '+t('on average')),
   tile(t('Block chance'),odds(j.hashrate,j.network.difficulty,86400)+' / '+t('day'),odds(j.hashrate,j.network.difficulty,31557600)+' / '+t('year')),
   tile(t('Blocks found'),j.blocks,t('lifetime')+' '+j.lifetime.blocks),
   tile(t('Total hashes'),si(j.total_hashes),t('uptime')+' '+dur(j.uptime_s)),
   tile(t('Block height'),j.network.height||'-',t('net diff')+' '+diff(j.network.difficulty)),
   tile(t('Lifetime'),si(j.lifetime.hashes)+' '+t('hashes'),j.lifetime.shares+' '+t('shares')+' · '+t('running')+' '+dur(j.lifetime.seconds)),
   chart(cachedHistory)];
  const u=upd(j.github_update);document.getElementById('updates').replaceChildren(...(u?[u]:[]));
  if(pv.updated_s>=0)T.push(tile(t('Seen by the pool'),si(pv.hashrate,'H/s'),t('best')+' '+diff(pv.best)+' · '+pv.workers+' '+t('worker(s)')+' · '+Math.round(pv.updated_s/60)+' '+t('min ago')));
  else if(!pv.supported)T.push(tile(t('Seen by the pool'),'-',t('no stats API known for this pool')));
  const now=Date.now()/1e3,B=j.blocks_recent||[];
  if(B.length)T.push(tile(t('Recent blocks'),B[0].mining?B[0].h+' · '+t('mining now'):B[0].h,B.filter(b=>!b.mining).slice(0,6).map(b=>b.h+' · '+(b.pool||'?')+' · '+(LANG=='fr'?t('ago')+' '+dur(now-b.t):dur(now-b.t)+' '+t('ago'))+' · '+b.tx+' tx'+(b.best!=null?' · '+t('your best')+' '+diff(b.best):'')).join('\n')+(B[0].mining&&B[0].best!=null?'\n'+t('mining now')+': '+t('your best')+' '+diff(B[0].best):''),true));
  if(j.top&&j.top.length)T.push(tile(t('Best shares'),j.top.slice(0,5).map(x=>diff(x.d)).join('  '),j.top.slice(0,5).map(x=>x.t?new Date(x.t*1e3).toLocaleDateString():'-').join(' · '),true));
  if(m.updated_s>=0){T.push(tile(t('BTC price'),m.price?Math.round(m.price).toLocaleString()+' '+m.currency:'-',m.price?Math.round(1e8/m.price).toLocaleString()+' '+t('sats')+' / '+m.currency:'',true),
   tile(t('Fees sat/vB'),m.fees.join(' / '),t('fast / 30 min / 1 h')),
   tile(t('Network hashrate'),si(m.hashrate,'H/s'),m.mempool_tx.toLocaleString()+' '+t('tx in mempool')),
   tile(t('Next retarget'),(m.retarget.change>=0?'+':'')+m.retarget.change.toFixed(2)+' %',t('in')+' '+m.retarget.blocks+' '+t('blocks')+' ('+m.retarget.progress.toFixed(0)+' % '+t('done')+')'))}
  T.push(tile(t(j.pool.backup?'Pool (backup)':'Pool'),j.pool.host+':'+j.pool.port,j.pool.status),
   tile(t('Payout address'),j.address,j.address_type==='invalid'?'⚠ '+t('not a valid Bitcoin address'):j.address_type),
   tile(t('Temperature'),j.temp_c==null?'-':j.temp_c+' °C',(j.temperature_source||'')+(j.max_temp_c?' · '+t('Configured limit')+': '+j.max_temp_c+' °C':'')),
   tile(t('Effective power'),(j.power_percent??'-')+' %',t('Configured limit')+': '+(j.power_limit_percent??'-')+' %'),
   tile(t('Memory'),si(j.heap,'B'),t('largest block')+': '+si(j.heap_max_block,'B')),
   tile(t('Device'),j.board,'v'+j.firmware+(j.temp_c==null?'':' · '+j.temp_c+' °C')+' · '+Math.round(j.heap/1024)+' KB'+(j.night?' · '+t('night mode'):'')+'\n'+t('last reset')+': '+j.boot_reason),
   tile(t('WiFi'),j.ip,j.ssid+' · '+j.rssi+' dBm'));
  for(const [group,id]of Object.entries({pool:'pool-grid',market:'market-grid',device:'device-grid',main:'m'}))document.getElementById(id).replaceChildren(...T.filter(e=>(e.dataset.group||'main')===group));
  document.getElementById('market-note').textContent=m.updated_s>=0?'':t('Public market data is temporarily unavailable. Mining can continue.');
  renderHealth(j);
  document.getElementById('f').textContent=t('Updated')+' '+new Date().toLocaleTimeString()+(m.updated_s>=0?' · '+t('market data')+' '+Math.round(m.updated_s/60)+' '+t('min old')+' (mempool.space)':'');

  const hosts=peerHosts(j.peers,j.ip);
  document.getElementById('ph').hidden=!hosts.length;
  if(hosts.length){
   const replies=await Promise.all(hosts.map(peer));let total=j.hashrate;
   const P=hosts.map((u,i)=>{const p=replies[i];if(!p)return tile(u.host,'-',t('no answer'));total+=p.hashrate;return tile(u.host,si(p.hashrate,'H/s'),(p.pool.connected?t('online'):t('OFFLINE'))+' · '+p.shares.accepted+' '+t('shares')+' · '+t('best')+' '+diff(p.best.ever))});
   P.unshift(tile(t('All miners'),si(total,'H/s'),(hosts.length+1)+' '+t('miners'),true));document.getElementById('p').replaceChildren(...P);
  }else document.getElementById('p').replaceChildren();
  await refreshHistory(j);
 }catch(e){const st=document.getElementById('st');st.textContent=t('NO CONNECTION');st.style.color='var(--bad)';document.getElementById('f').textContent=errorText(e)}
}
startPolling(tick,3000);

function renderHealth(j){
 const reasons={sensor:'No usable temperature sensor.',thermal:'Temperature protection is limiting mining.',hot:'Temperature protection has paused mining.',user_pause:'Mining is paused by your power setting.',ota:'Mining is paused during a firmware update.',supervision:'Mining is waiting for fresh supervision.',battery:'Battery saver is limiting mining.',user:'Mining is limited by your power setting.'};
 const reason=(j.task_failures?'A service could not start: not enough memory. Restart the miner.':'')||reasons[j.limitation_reason]||(!j.pool.connected?'Waiting for a pool connection.':'');
 const box=document.getElementById('health');box.hidden=!reason;box.textContent=t(reason);box.dataset.level=['sensor','hot','supervision'].includes(j.limitation_reason)?'bad':'info';
}

async function refreshHistory(j){
 if(j.history||j.history_revision==null||historyRevision===j.history_revision||Date.now()<historyRetryAt)return;
 try{
  const h=await request('/api/history');
  if(!h||!Array.isArray(h.hashrate)||!Array.isArray(h.temperature_c)||!Array.isArray(h.power_percent)||h.hashrate.length!==h.temperature_c.length||h.hashrate.length!==h.power_percent.length)throw Error(t('Invalid response from the miner.'));
  cachedHistory=h;historyRevision=h.revision??j.history_revision;historyRetryAt=0;
  document.getElementById('hashrate-history').replaceWith(chart(h));
  document.getElementById('history-grid').replaceChildren(series(t('Temperature'),'temperature_c','°C','#4fa7ff'),series(t('Effective power'),'power_percent','%','var(--accent)'));
  document.getElementById('download-history').disabled=!h.hashrate.length;
  document.getElementById('history-status').textContent='';
 }catch(e){historyRetryAt=Date.now()+60000;document.getElementById('history-status').textContent=errorText(e)}
}
function series(label,key,unit,color){
 const values=cachedHistory[key]||[],valid=values.filter(Number.isFinite),last=values[values.length-1];
 const card=tile(label,Number.isFinite(last)?last.toFixed(1)+' '+unit:'-',valid.length?t('range')+' '+Math.min(...valid).toFixed(1)+'–'+Math.max(...valid).toFixed(1)+' '+unit:t('No observations yet.'),true);
 if(valid.length>1){
  const ns='http://www.w3.org/2000/svg',svg=document.createElementNS(ns,'svg'),path=document.createElementNS(ns,'path'),lo=key==='power_percent'?0:Math.min(...valid)-1,hi=key==='power_percent'?100:Math.max(...valid)+1,scale=96/(hi-lo||1);let drawing=false,d='';
  values.forEach((v,i)=>{if(!Number.isFinite(v)){drawing=false;return}d+=(drawing?'L':'M')+(i*600/Math.max(1,values.length-1)).toFixed(1)+','+(98-(v-lo)*scale).toFixed(1)+' ';drawing=true});
  svg.setAttribute('viewBox','0 0 600 100');svg.setAttribute('preserveAspectRatio','none');svg.style.cssText='width:100%;height:100px;display:block';path.setAttribute('d',d);path.setAttribute('fill','none');path.setAttribute('stroke',color);path.setAttribute('stroke-width','2');path.setAttribute('vector-effect','non-scaling-stroke');svg.appendChild(path);card.appendChild(svg);
 }
 return card;
}
function downloadText(name,text,type){const url=URL.createObjectURL(new Blob([text],{type})),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)}
document.getElementById('download-history').onclick=()=>{
 const h=cachedHistory,rows=['minutes_before_latest,hashrate_hps,temperature_c,power_percent,coverage_percent'];
 h.hashrate.forEach((v,i)=>rows.push([(h.hashrate.length-1-i)*(h.interval_s||300)/60,v,h.temperature_c?.[i]??'',h.power_percent?.[i]??'',h.coverage_percent?.[i]??''].join(',')));
 downloadText('bdlminer-history.csv',rows.join('\n')+'\n','text/csv;charset=utf-8');
};
let latestDiagnostics=null;
async function refreshDiagnostics(){
 try{
  const d=await request('/api/diagnostics');
  if(!d?.tls||!Array.isArray(d.engines))throw Error(t('Invalid response from the miner.'));
  latestDiagnostics=d;
  const cards=[tile(t('TLS handshakes'),d.tls.handshakes,t('Elapsed time')+': '+dur(d.tls.total_ms/1000)+' · '+d.tls.failures+' '+t('failures'),true)];
  for(const e of d.engines)cards.push(tile(t('Mining worker')+' '+e.index+' · '+e.name,dur(e.hashing_ms/1000),t('Batch time')+'\n'+t('Rest')+': '+dur(e.rest_ms/1000)+' · '+t('Waiting for work')+': '+dur(e.waiting_ms/1000)+' · '+t('Paused')+': '+dur(e.paused_ms/1000),true));
  document.getElementById('profile-grid').replaceChildren(...cards);document.getElementById('profile-status').textContent='';
 }catch(e){document.getElementById('profile-status').textContent=errorText(e)}
}
startPolling(refreshDiagnostics,10000);

let benchmark=null,benchmarkBusy=false,benchmarkReport=null,benchmarkCancelled=false,benchmarkFinishing=false;
const benchmarkStatus=document.getElementById('benchmark-status');
function measurementSample(j){return {uptime_s:j.uptime_s,total_hashes:j.total_hashes,temperature_c:j.temp_c??null,power_percent:j.power_percent,configured_power_percent:j.power_limit_percent,max_temperature_c:j.max_temp_c??null,temperature_source:j.temperature_source||'unavailable',reason:j.limitation_reason||'unknown'}}
function benchmarkControls(){document.getElementById('start-benchmark').disabled=!latestStats||benchmarkBusy||!!benchmark;document.getElementById('stop-benchmark').disabled=!benchmark||benchmarkBusy;document.getElementById('download-benchmark').disabled=!benchmarkReport||benchmarkBusy}
async function startBenchmark(){
 if(!latestStats||benchmark||benchmarkBusy)return;
 benchmarkBusy=true;benchmarkCancelled=false;benchmarkControls();benchmarkStatus.textContent=t('Preparing measurement…');
 const start=measurementSample(latestStats),firmware=latestStats.firmware,bootId=latestStats.boot_id;
 try{
  const profile=await request('/api/diagnostics');
  if(!profile?.tls||!Array.isArray(profile.engines)||!Number.isFinite(profile.uptime_s))throw Error(t('Invalid response from the miner.'));
  if(benchmarkCancelled||document.hidden||!latestStats||latestStats.uptime_s<start.uptime_s||(bootId&&profile.boot_id&&bootId!==profile.boot_id)){benchmarkStatus.textContent=t('Measurement cancelled. Keep this tab visible.');return}
  benchmark={start,firmware,bootId,board:latestStats.board,profileStart:profile,samples:[start],gap:false,startedAt:new Date().toISOString()};benchmarkReport=null;
  benchmarkStatus.textContent=t('Measurement running…');
 }catch(e){benchmarkStatus.textContent=errorText(e)}finally{benchmarkBusy=false;benchmarkControls()}
}
function sampleBenchmark(j){
 if(!benchmark)return;
 if(j.firmware!==benchmark.firmware||(benchmark.bootId&&j.boot_id&&j.boot_id!==benchmark.bootId)||j.uptime_s<benchmark.samples[benchmark.samples.length-1].uptime_s||j.total_hashes<benchmark.samples[benchmark.samples.length-1].total_hashes){cancelBenchmark(t('Miner restarted. Start a new measurement.'));return}
 const previous=benchmark.samples[benchmark.samples.length-1];
 if(j.uptime_s<=previous.uptime_s)return;
 if(j.uptime_s-previous.uptime_s>15)benchmark.gap=true;
 benchmark.samples.push(measurementSample(j));benchmarkStatus.textContent=t('Measurement running…')+' '+dur(j.uptime_s-benchmark.start.uptime_s)+' / 10m';
 if(j.uptime_s-benchmark.start.uptime_s>=600)finishBenchmark();
}
function cancelBenchmark(message){benchmark=null;benchmarkCancelled=true;benchmarkStatus.textContent=message;benchmarkControls()}
async function finishBenchmark(){
 if(!benchmark||benchmarkBusy)return;
 const b=benchmark,end=b.samples[b.samples.length-1],elapsed=end.uptime_s-b.start.uptime_s;
 if(!(elapsed>0)){benchmarkStatus.textContent=t('Waiting for a new sample.');return}
 benchmark=null;benchmarkBusy=true;benchmarkFinishing=true;benchmarkControls();
 const temps=b.samples.map(s=>s.temperature_c).filter(Number.isFinite),power=b.samples.map(s=>s.power_percent).filter(Number.isFinite);
 const report={format:'bdlminer-measurement-v1',client_started_at:b.startedAt,client_finished_at:new Date().toISOString(),firmware:b.firmware,boot_id:b.bootId,board:b.board,elapsed_s:elapsed,hashes:end.total_hashes-b.start.total_hashes,hashrate_hps:(end.total_hashes-b.start.total_hashes)/elapsed,temperature_min_c:temps.length?Math.min(...temps):null,temperature_max_c:temps.length?Math.max(...temps):null,power_min_percent:power.length?Math.min(...power):null,power_max_percent:power.length?Math.max(...power):null,sample_gaps:b.gap,samples:b.samples,profiling_start:b.profileStart};
 try{const endProfile=await request('/api/diagnostics');if(endProfile.uptime_s<b.profileStart.uptime_s||endProfile.firmware!==b.profileStart.firmware||(endProfile.boot_id&&b.profileStart.boot_id&&endProfile.boot_id!==b.profileStart.boot_id))throw Error(t('Miner restarted. Start a new measurement.'));report.profiling_end=endProfile;report.profiling_delta={elapsed_s:endProfile.uptime_s-b.profileStart.uptime_s,tls:{handshakes:endProfile.tls.handshakes-b.profileStart.tls.handshakes,failures:endProfile.tls.failures-b.profileStart.tls.failures,total_ms:endProfile.tls.total_ms-b.profileStart.tls.total_ms},engines:endProfile.engines.map(e=>{const before=b.profileStart.engines.find(p=>p.index===e.index&&p.name===e.name);if(!before)return null;const delta={index:e.index,name:e.name};for(const key of ['hashing_ms','rest_ms','waiting_ms','paused_ms'])delta[key]=Math.max(0,e[key]-before[key]);return delta}).filter(Boolean)}}catch(e){report.profiling_error=errorText(e)}
 benchmarkReport=report;benchmarkBusy=false;benchmarkFinishing=false;benchmarkControls();benchmarkStatus.textContent=t('Measured hashrate')+': '+si(report.hashrate_hps,'H/s')+' · '+dur(elapsed)+(b.gap?' · '+t('Temperature samples contain gaps.'):'');
}
document.getElementById('start-benchmark').onclick=startBenchmark;
document.getElementById('stop-benchmark').onclick=finishBenchmark;
document.getElementById('download-benchmark').onclick=()=>{if(benchmarkReport)downloadText('bdlminer-measurement.json',JSON.stringify(benchmarkReport,null,2),'application/json')};
document.addEventListener('visibilitychange',()=>{if(document.hidden&&(benchmark||benchmarkBusy&&!benchmarkFinishing))cancelBenchmark(t('Measurement cancelled. Keep this tab visible.'))});
