let cachedHistory={hashrate:[],interval_s:300},historyRevision=null,historyRetryAt=0,lastUptime=-1,lastBootId=null,latestStats=null;
let historyRange=21600,historySelected=null,plotViews=[],historyWidth=0,historySignature='',lastChartLimit=null;
let historyPointerFrame=0,peerCacheKey='',peerCache=[],peerCacheAt=0;
try{const saved=Number(localStorage.getItem('bdl-history-range'));if([3600,21600,86400].includes(saved))historyRange=saved}catch(e){}
const SI=['','k','M','G','T','P','E','Z','Y'];
function si(v,u=''){if(v==null||!Number.isFinite(v))return'-';if(v===0)return'0'+(u?' '+u:'');let i=0;while(Math.abs(v)>=1000&&i<SI.length-1){v/=1000;i++}return v.toFixed(v<10?2:v<100?1:0)+' '+SI[i]+u}
function diff(d){return!d?'-':d<1?d.toFixed(5):si(d)}
function dur(s){if(!Number.isFinite(s))return'-';s=Math.max(0,s);if(s<60)return Math.round(s)+'s';const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;return(d?d+(LANG=='fr'?'j ':'d '):'')+(d||h?h+'h ':'')+m+'m'}
function share64Wait(hr){if(!(hr>0))return'-';const s=18446744073709551616/hr;return s>=31557600?si(s/31557600)+' '+t('years'):dur(s)}
function odds(hr,nd,s){if(!hr||!nd)return'-';const p=-Math.expm1(-hr*s/(nd*4294967296));return(LANG=='fr'?'1 sur ':'1 in ')+si(1/p).replace(' ','')}
function tile(label,value,sub,big){const e=document.createElement('div');e.className='t'+(big?' big':'');
 if(label===t('Hashrate'))e.classList.add('hero');
 if(label===t('Hashrate history'))e.classList.add('history');
 for(const [group,names]of Object.entries({pool:['Pool','Pool (backup)','Payout address','Seen by the pool'],market:['BTC price','Fees sat/vB','Network hashrate','Next retarget','Recent blocks'],device:['Device','WiFi','Temperature','Memory','Effective power'],records:['32-bit shares','64-bit shares','Block chance','Blocks found','Total hashes','Block height','Lifetime','Best shares']}))if(names.some(name=>label===t(name)))e.dataset.group=group;
 for(const[c,x]of[['l',label],['v',value],['s',sub]])if(x!==undefined){const d=document.createElement('div');d.className=c;d.textContent=x;e.appendChild(d)}return e}

function textAt(id,value){const node=document.getElementById(id);if(node.textContent!==String(value))node.textContent=value}
function syncTiles(id,nodes){
 const parent=document.getElementById(id),old=new Map([...parent.children].map(e=>[e.querySelector('.l')?.textContent,e]));
 nodes.forEach((fresh,i)=>{
  const key=fresh.querySelector('.l')?.textContent,node=old.get(key)||fresh;old.delete(key);
  if(node!==fresh){node.className=fresh.className;if(node.children.length!==fresh.children.length)node.replaceChildren(...fresh.childNodes);else [...fresh.children].forEach((child,j)=>{if(node.children[j].textContent!==child.textContent)node.children[j].textContent=child.textContent})}
  if(parent.children[i]!==node)parent.insertBefore(node,parent.children[i]||null);
 });
 old.forEach(node=>node.remove());
}

let installPending=false,installRequestId=0,installError='',checkPending=false;
let updateSignature='';
function renderUpdate(u){
 const signature=JSON.stringify([u,installPending,installError,checkPending]);if(signature===updateSignature)return;
 updateSignature=signature;const focused=document.activeElement?.classList.contains('gh-check')?'gh-check':document.activeElement?.classList.contains('gh-install')?'gh-install':null;
 const card=upd(u);document.getElementById('updates').replaceChildren(...(card?[card]:[]));
 if(focused)document.querySelector('#updates .'+focused+':enabled')?.focus({preventScroll:true});
}
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
  if(j.uptime_s<lastUptime||(lastBootId&&j.boot_id&&j.boot_id!==lastBootId)){historyRevision=null;cachedHistory={hashrate:[],interval_s:300};historyRetryAt=0;historySignature='';historySelected=null;renderHistory()}
  lastUptime=j.uptime_s;lastBootId=j.boot_id||null;latestStats=j;
  if(j.history){const signature=JSON.stringify(j.history);if(signature!==historySignature){cachedHistory=normalizeHistory(j.history);historySignature=signature;renderHistory()}}
  sampleBenchmark(j);
  document.getElementById('start-benchmark').disabled=benchmarkBusy||!!benchmark;
  document.documentElement.style.setProperty('--accent',j.accent);
  const st=document.getElementById('st');textAt('st',t(j.pool.connected?'ONLINE':'OFFLINE'));st.style.color=j.pool.connected?'var(--ok)':'var(--bad)';st.dataset.state=j.pool.connected?'online':'disconnected';
  const state=j.throttle=='paused'?'⚠ '+t('mining paused'):j.throttle=='half'?t('battery saver: half speed'):j.throttle=='reduced'?t('power limited'):'';
  const T=[tile(t('Hashrate'),si(j.hashrate,'H/s'),t('1-minute average')+(Number.isFinite(j.hashrate_live)?' · '+t('Live (10 s)')+': '+si(j.hashrate_live,'H/s'):'')+'\n'+j.engines.map(e=>e.name+' '+si(e.rate,'H/s')).join(' + ')+(state?'\n'+state+' · '+j.power_percent+' %':''),true),
   tile(t('Shares'),j.shares.accepted,t('accepted')+(j.shares.rejected?' · '+j.shares.rejected+' '+t('rejected'):'')+'\n'+t('pool diff')+' '+diff(j.pool.difficulty)+(j.pool.pending_shares?' · '+j.pool.pending_shares+' '+t('awaiting response'):'')),
   tile(t('Best difficulty'),diff(j.best.session),t('best ever')+' '+diff(j.best.ever)),
   tile(t('32-bit shares'),j.over32bits,t('1 every')+' '+(j.hashrate?dur(4294967296/j.hashrate):'-')+' '+t('on average')),
   tile(t('64-bit shares'),j.over64bits??0,t('Since restart')+' · '+t('1 every')+' '+share64Wait(j.hashrate)+' '+t('on average')),
   tile(t('Block chance'),odds(j.hashrate,j.network.difficulty,86400)+' / '+t('day'),odds(j.hashrate,j.network.difficulty,31557600)+' / '+t('year')),
   tile(t('Blocks found'),j.blocks,t('lifetime')+' '+j.lifetime.blocks),
   tile(t('Total hashes'),si(j.total_hashes),t('uptime')+' '+dur(j.uptime_s)),
   tile(t('Block height'),j.network.height||'-',t('net diff')+' '+diff(j.network.difficulty)),
   tile(t('Lifetime'),si(j.lifetime.hashes)+' '+t('hashes'),j.lifetime.shares+' '+t('shares')+' · '+t('running')+' '+dur(j.lifetime.seconds))];
  renderUpdate(j.github_update);
  if(pv.updated_s>=0)T.push(tile(t('Seen by the pool'),si(pv.hashrate,'H/s'),t('best')+' '+diff(pv.best)+' · '+pv.workers+' '+t('worker(s)')+' · '+Math.round(pv.updated_s/60)+' '+t('min ago')));
  else if(!pv.supported)T.push(tile(t('Seen by the pool'),'-',t('no stats API known for this pool')));
  const now=Date.now()/1e3,B=j.blocks_recent||[];
  if(B.length)T.push(tile(t('Recent blocks'),B[0].mining?B[0].h+' · '+t('mining now'):B[0].h,B.filter(b=>!b.mining).slice(0,6).map(b=>b.h+' · '+(b.pool||'?')+' · '+(LANG=='fr'?t('ago')+' '+dur(now-b.t):dur(now-b.t)+' '+t('ago'))+' · '+b.tx+' tx'+(b.best!=null?' · '+t('your best')+' '+diff(b.best):'')).join('\n')+(B[0].mining&&B[0].best!=null?'\n'+t('mining now')+': '+t('your best')+' '+diff(B[0].best):''),true));
  if(j.top&&j.top.length)T.push(tile(t('Best shares'),j.top.slice(0,5).map(x=>diff(x.d)).join('  '),j.top.slice(0,5).map(x=>x.t?new Date(x.t*1e3).toLocaleDateString():'-').join(' · '),true));
  if(m.updated_s>=0){T.push(tile(t('BTC price'),m.price?Math.round(m.price).toLocaleString()+' '+m.currency:'-',m.price?Math.round(1e8/m.price).toLocaleString()+' '+t('sats')+' / '+m.currency:'',true),
   tile(t('Fees sat/vB'),m.fees.join(' / '),t('fast / 30 min / 1 h')),
   tile(t('Network hashrate'),si(m.hashrate,'H/s'),m.mempool_tx.toLocaleString()+' '+t('tx in mempool')),
   tile(t('Next retarget'),(m.retarget.change>=0?'+':'')+m.retarget.change.toFixed(2)+' %',t('in')+' '+m.retarget.blocks+' '+t('blocks')+' ('+m.retarget.progress.toFixed(0)+' % '+t('done')+')'))}
  T.push(tile(t(j.pool.backup?'Pool (backup)':'Pool'),j.pool.host+':'+j.pool.port,j.pool.status+(j.pool.user_agent?'\n'+(j.pool.protocol||'Stratum V1')+' · '+j.pool.user_agent+' · '+t('automatic'):'')),
   tile(t('Payout address'),j.address,j.address_type==='invalid'?'⚠ '+t('not a valid Bitcoin address'):j.address_type),
   tile(t('Temperature'),j.temp_c==null?'-':j.temp_c+' °C',(j.temperature_source||'')+(j.max_temp_c?' · '+t('Configured limit')+': '+j.max_temp_c+' °C':'')),
   tile(t('Effective power'),(j.power_percent??'-')+' %',t('Configured limit')+': '+(j.power_limit_percent??'-')+' %'),
   tile(t('Memory'),si(j.heap,'B'),t('largest block')+': '+si(j.heap_max_block,'B')),
   tile(t('Device'),j.board,'v'+j.firmware+(j.temp_c==null?'':' · '+j.temp_c+' °C')+' · '+Math.round(j.heap/1024)+' KB'+(j.night?' · '+t('night mode'):'')+'\n'+t('last reset')+': '+j.boot_reason),
   tile(t('WiFi'),j.ip,j.ssid+' · '+j.rssi+' dBm'));
  for(const [group,id]of Object.entries({pool:'pool-grid',market:'market-grid',device:'device-grid',records:'records-grid',main:'m'}))syncTiles(id,T.filter(e=>(e.dataset.group||'main')===group));
  renderOverview(j);
  if(lastChartLimit!==j.max_temp_c){lastChartLimit=j.max_temp_c;renderHistory()}
  document.getElementById('market-note').textContent=m.updated_s>=0?'':t('Public market data is temporarily unavailable. Mining can continue.');
  renderHealth(j);
  document.getElementById('f').textContent=t('Updated')+' '+new Date().toLocaleTimeString()+(m.updated_s>=0?' · '+t('market data')+' '+Math.round(m.updated_s/60)+' '+t('min old')+' (mempool.space)':'');

  await refreshHistory(j);
  const hosts=peerHosts(j.peers,j.ip);
  document.getElementById('ph').hidden=!hosts.length;
  document.getElementById('peers-section').hidden=!hosts.length;
  if(hosts.length){
   const key=hosts.map(u=>u.origin).join('|');if(key!==peerCacheKey||Date.now()-peerCacheAt>=15000){peerCache=await Promise.all(hosts.map(peer));peerCacheKey=key;peerCacheAt=Date.now()}
   const replies=peerCache;let total=j.hashrate;
   const P=hosts.map((u,i)=>{const p=replies[i];if(!p)return tile(u.host,'-',t('no answer'));total+=p.hashrate;return tile(u.host,si(p.hashrate,'H/s'),(p.pool.connected?t('online'):t('OFFLINE'))+' · '+p.shares.accepted+' '+t('shares')+' · '+t('best')+' '+diff(p.best.ever))});
   P.unshift(tile(t('All miners'),si(total,'H/s'),(hosts.length+1)+' '+t('miners'),true));syncTiles('p',P);
  }else{document.getElementById('p').replaceChildren();peerCacheKey='';peerCache=[]}
 }catch(e){const st=document.getElementById('st');textAt('st',t('NO CONNECTION'));st.style.color='var(--bad)';st.dataset.state='disconnected';textAt('f',errorText(e))}
}
startPolling(tick,3000);

function renderHealth(j){
 const reasons={sensor:'No usable temperature sensor.',thermal:'Temperature protection is limiting mining.',hot:'Temperature protection has paused mining.',user_pause:'Mining is paused by your power setting.',ota:'Mining is paused during a firmware update.',supervision:'Mining is waiting for fresh supervision.',battery:'Battery saver is limiting mining.',user:'Mining is limited by your power setting.'};
 const reason=(j.task_failures?'A service could not start: not enough memory. Restart the miner.':'')||reasons[j.limitation_reason]||(!j.pool.connected?'Waiting for a pool connection.':'');
 const box=document.getElementById('health');box.hidden=!reason;box.textContent=t(reason);box.dataset.level=['sensor','hot','supervision'].includes(j.limitation_reason)?'bad':'info';
}

function renderOverview(j){
 const values=[[t('Temperature'),j.temp_c==null?'—':j.temp_c+' °C',j.max_temp_c&&j.temp_c>=j.max_temp_c-5],[t('Effective power'),(j.power_percent??'—')+' %',false],[t('uptime'),dur(j.uptime_s),false]];
 const host=document.getElementById('overview-strip');
 values.forEach(([label,value,warm],i)=>{let node=host.children[i];if(!node){node=document.createElement('div');node.append(document.createElement('span'),document.createElement('strong'));host.appendChild(node)}if(node.children[0].textContent!==label)node.children[0].textContent=label;if(node.children[1].textContent!==value)node.children[1].textContent=value;node.children[1].classList.toggle('warm',!!warm)});
}

async function refreshHistory(j){
 if(j.history||j.history_revision==null||historyRevision===j.history_revision||Date.now()<historyRetryAt)return;
 try{
  const h=await request('/api/history');
  const normalized=normalizeHistory(h);
  if(h.boot_id&&j.boot_id&&h.boot_id!==j.boot_id)return;
  cachedHistory=normalized;historyRevision=h.revision??j.history_revision;historyRetryAt=0;historySelected=null;
  renderHistory();
  document.getElementById('history-status').textContent='';
 }catch(e){historyRetryAt=Date.now()+60000;document.getElementById('history-status').textContent=errorText(e)}
}
function normalizeHistory(h){
 if(!h||!Array.isArray(h.hashrate)||h.hashrate.length>288||!Number.isFinite(h.interval_s)||h.interval_s<1||h.interval_s>86400)throw Error(t('Invalid response from the miner.'));
 const copy={...h};
 for(const key of ['hashrate','temperature_c','power_percent','coverage_percent']){
  const values=h[key]??Array(h.hashrate.length).fill(null);
  if(!Array.isArray(values)||values.length!==h.hashrate.length||values.some(v=>v!==null&&(!Number.isFinite(v)||(key==='hashrate'&&v<0)||(['power_percent','coverage_percent'].includes(key)&&(v<0||v>100)))))throw Error(t('Invalid response from the miner.'));
  copy[key]=values.slice();
 }
 return copy;
}
function visibleHistory(){
 const h=cachedHistory,n=Math.floor(historyRange/(h.interval_s||300))+1,result={interval_s:h.interval_s||300};
 for(const key of ['hashrate','temperature_c','power_percent','coverage_percent'])result[key]=(h[key]||Array(h.hashrate.length).fill(null)).slice(-n);
 return result;
}
function svgNode(name,attributes={},text){
 const node=document.createElementNS('http://www.w3.org/2000/svg',name);
 for(const [key,value]of Object.entries(attributes))node.setAttribute(key,value);
 if(text!==undefined)node.textContent=text;
 return node;
}
function historyDuration(seconds){const minutes=Math.round(seconds/60);return minutes<60?minutes+' min':Math.floor(minutes/60)+'h'+(minutes%60?String(minutes%60).padStart(2,'0'):'')}
function sampleAge(index,count,interval){return index===count-1?t('Latest point'):'−'+historyDuration((count-1-index)*interval)}
function niceStep(value){
 if(!(value>0))return 1;
 const base=10**Math.floor(Math.log10(value)),part=value/base;
 return (part<=1?1:part<=2?2:part<=2.5?2.5:part<=5?5:10)*base;
}
function drawPlot(host,key,values,color,small=false){
 const width=Math.max(240,Math.round(host.getBoundingClientRect().width)),height=small?165:host.clientHeight||275;
 const left=small?45:61,right=width-16,top=18,bottom=height-31,valid=values.filter(Number.isFinite);
 let low=0,high=100,step=25,limit=Number(latestStats?.max_temp_c)||0;
 if(key==='hashrate'){const peak=Math.max(0,...valid)*1.06;step=niceStep(peak/3);high=Math.max(step,Math.ceil(peak/step)*step)}
 if(key==='temperature_c'){const refs=limit>0?[...valid,limit]:valid;const min=refs.length?Math.min(...refs):0,max=refs.length?Math.max(...refs):80;step=niceStep((max-min+4)/4);low=Math.floor((min-2)/step)*step;high=Math.ceil((max+2)/step)*step}
 const unit=key==='hashrate'?'H/s':key==='temperature_c'?'°C':'%',divisor=key==='hashrate'?(high>=1e9?1e9:high>=1e6?1e6:high>=1e3?1e3:1):1;
 const axisUnit=key==='hashrate'?(divisor===1e9?'GH/s':divisor===1e6?'MH/s':divisor===1e3?'kH/s':'H/s'):unit;
 const x=i=>values.length===1?right:left+i*(right-left)/Math.max(1,values.length-1),y=v=>bottom-(v-low)*(bottom-top)/(high-low||1);
 const svg=svgNode('svg',{viewBox:`0 0 ${width} ${height}`,preserveAspectRatio:'none','aria-hidden':'true'});
 svg.appendChild(svgNode('text',{x:small?10:12,y:11,class:'plot-axis'},axisUnit));
 for(let i=0;i<=Math.round((high-low)/step);++i){const value=low+i*step,py=y(value);svg.append(svgNode('line',{x1:left,y1:py,x2:right,y2:py,class:'plot-grid'}),svgNode('text',{x:left-9,y:py+3,'text-anchor':'end',class:'plot-axis'},(value/divisor).toLocaleString(LANG,{maximumFractionDigits:divisor>1?2:1})))}
 const ticks=width>550?5:3;
 if(values.length){const used=new Set();for(let i=0;i<ticks;++i){const index=Math.round(i*(values.length-1)/(ticks-1));if(used.has(index))continue;used.add(index);svg.appendChild(svgNode('text',{x:x(index),y:height-8,'text-anchor':i===0&&values.length>1?'start':index===values.length-1?'end':'middle',class:'plot-axis'},sampleAge(index,values.length,cachedHistory.interval_s||300)))}}
 const segments=[];let current=[];
 values.forEach((value,index)=>{if(Number.isFinite(value))current.push([x(index),y(value)]);else if(current.length){segments.push(current);current=[]}});if(current.length)segments.push(current);
 const path=segments.map(segment=>segment.map(([px,py],i)=>(i?'L':'M')+px.toFixed(1)+','+py.toFixed(1)).join(' ')).join(' ');
 if(!small){const id='history-area-'+key,defs=svgNode('defs'),gradient=svgNode('linearGradient',{id,x1:'0',y1:'0',x2:'0',y2:'1'});gradient.append(svgNode('stop',{offset:'0%','stop-color':color,'stop-opacity':'.23'}),svgNode('stop',{offset:'100%','stop-color':color,'stop-opacity':'.01'}));defs.appendChild(gradient);svg.appendChild(defs);const area=segments.filter(s=>s.length>1).map(segment=>segment.map(([px,py],i)=>(i?'L':'M')+px.toFixed(1)+','+py.toFixed(1)).join(' ')+` L${segment[segment.length-1][0]},${bottom} L${segment[0][0]},${bottom} Z`).join(' ');svg.appendChild(svgNode('path',{d:area,fill:`url(#${id})`}))}
 if(key==='temperature_c'&&limit>0&&valid.length)svg.appendChild(svgNode('line',{x1:left,y1:y(limit),x2:right,y2:y(limit),class:'plot-limit'}));
 svg.appendChild(svgNode('path',{d:path,stroke:color,class:'plot-line','data-series':key}));
 for(const segment of segments)if(segment.length===1)svg.appendChild(svgNode('circle',{cx:segment[0][0],cy:segment[0][1],r:3,fill:color}));
 const cursor=svgNode('line',{x1:right,y1:top,x2:right,y2:bottom,class:'plot-cursor',visibility:'hidden'}),dot=svgNode('circle',{cx:right,cy:bottom,r:4,fill:color,class:'plot-dot',visibility:'hidden'});svg.append(cursor,dot);
 host.replaceChildren(svg);
 if(!valid.length){const empty=document.createElement('div');empty.className='plot-empty';const title=document.createElement('span');title.textContent=t('No observations yet.');const note=document.createElement('small');note.textContent=t(key==='temperature_c'?'A usable temperature sensor is required.':'The first history point appears after five minutes of mining.');empty.append(title,note);host.appendChild(empty)}
 const view={host,values,x,y,left,right,width,cursor,dot};plotViews.push(view);
 const point=event=>{if(!values.length)return;document.getElementById('chart-inspection').setAttribute('aria-live','off');const bounds=host.getBoundingClientRect(),px=(event.clientX-bounds.left)*width/bounds.width,index=Math.max(0,Math.min(values.length-1,Math.round((px-left)/(right-left)*Math.max(1,values.length-1))));if(historyPointerFrame)cancelAnimationFrame(historyPointerFrame);historyPointerFrame=requestAnimationFrame(()=>{historyPointerFrame=0;selectHistory(index)})};
 host.onpointermove=point;host.onpointerdown=point;
 host.onpointerleave=event=>{if(event.pointerType==='touch')return;if(historyPointerFrame)cancelAnimationFrame(historyPointerFrame);historyPointerFrame=0;selectHistory(null)};
 return view;
}
function seriesCard(label,key,unit,color,history){
 const values=history[key],valid=values.filter(Number.isFinite),last=values[values.length-1],card=document.createElement('div');card.className='t big chart-series';
 const heading=document.createElement('div');heading.className='series-heading';
 const title=document.createElement('span');title.className='l';const marker=document.createElement('i');marker.className='legend-dot';marker.style.color=color;const name=document.createElement('span');name.textContent=label;title.append(marker,name);
 const value=document.createElement('span');value.className='v';value.style.color=color;value.textContent=Number.isFinite(last)?last.toFixed(1)+' '+unit:'—';heading.append(title,value);card.appendChild(heading);
 const range=document.createElement('div');range.className='series-range';range.textContent=valid.length?t('range')+' '+Math.min(...valid).toFixed(1)+'–'+Math.max(...valid).toFixed(1)+' '+unit:t('No observations yet.');
 if(key==='temperature_c'&&latestStats?.max_temp_c>0){const limit=document.createElement('span');limit.className='limit-key';limit.textContent=t('Configured limit')+' '+latestStats.max_temp_c+' °C';range.append(' · ',limit)}card.appendChild(range);
 const host=document.createElement('div');host.className='plot-host small-plot';card.appendChild(host);card._plot={host,key,values,color};return card;
}
function renderHistory(){
 const h=visibleHistory(),valid=h.hashrate.filter(Number.isFinite),mean=valid.length?valid.reduce((a,b)=>a+b,0)/valid.length:null;
 textAt('chart-average',mean===null?'—':si(mean,'H/s'));textAt('chart-min',valid.length?si(Math.min(...valid),'H/s'):'—');textAt('chart-max',valid.length?si(Math.max(...valid),'H/s'):'—');textAt('chart-count',valid.length);
 document.querySelectorAll('[data-range]').forEach(button=>button.setAttribute('aria-pressed',String(Number(button.dataset.range)===historyRange)));
 const cards=[seriesCard(t('Temperature'),'temperature_c','°C','#65b7ff',h),seriesCard(t('Effective power'),'power_percent','%','#50dba1',h)];document.getElementById('history-grid').replaceChildren(...cards);
 plotViews=[];drawPlot(document.getElementById('hashrate-history'),'hashrate',h.hashrate,'var(--accent)');for(const card of cards){const p=card._plot;drawPlot(p.host,p.key,p.values,p.color,true)}
 const duration=Math.max(0,h.hashrate.length-1)*h.interval_s;
 const scope=h.hashrate.length>1?t('Displayed history')+': '+historyDuration(duration):t(h.hashrate.length?'One recorded sample':'No observations yet.');
 textAt('history-caption',scope+' · '+t('Sampling interval')+': '+historyDuration(h.interval_s)+(h.hashrate.some(v=>v===null)||h.temperature_c.some(v=>v===null)?' · '+t('Gaps mean missing observations.') :''));
 document.getElementById('download-history').disabled=!h.hashrate.length;
 selectHistory(historySelected);
}
function selectHistory(index){
 const h=visibleHistory(),count=h.hashrate.length,next=index===null?null:Math.max(0,Math.min(count-1,index));if(next===historySelected&&plotViews[0]?.selectionReady)return;historySelected=next;const selected=historySelected??count-1;if(plotViews[0])plotViews[0].selectionReady=true;
 for(const view of plotViews){const value=view.values[selected],visible=historySelected!==null&&Number.isFinite(value);view.cursor.setAttribute('visibility',historySelected===null||!count?'hidden':'visible');view.dot.setAttribute('visibility',visible?'visible':'hidden');if(count){const px=view.x(selected);view.cursor.setAttribute('x1',px);view.cursor.setAttribute('x2',px);if(visible){view.dot.setAttribute('cx',px);view.dot.setAttribute('cy',view.y(value))}}}
 const inspector=document.getElementById('chart-inspection');
 if(!count){inspector.textContent=t('No observations yet.');return}
 const age=document.createElement('span');age.className='sample-age';age.textContent=sampleAge(selected,count,h.interval_s);
 const nodes=[age];for(const [key,color,unit,label]of [['hashrate','var(--accent)','H/s','Hashrate'],['temperature_c','#65b7ff','°C','Temperature'],['power_percent','#50dba1','%','Effective power']]){const value=h[key][selected],span=document.createElement('span');span.className='sample-value';const dot=document.createElement('i');dot.className='legend-dot';dot.style.color=color;dot.setAttribute('aria-hidden','true');const name=document.createElement('span');name.className='sr-only';name.textContent=t(label)+': ';const number=document.createElement('strong');number.style.color=color;number.textContent=!Number.isFinite(value)?'—':key==='hashrate'?si(value,unit):value.toFixed(1)+' '+unit;span.append(dot,name,number);nodes.push(span)}
 if(Number.isFinite(h.coverage_percent[selected])&&h.coverage_percent[selected]<100){const coverage=document.createElement('span');coverage.className='sample-coverage';coverage.textContent=t('Observation coverage')+': '+h.coverage_percent[selected]+' %';nodes.push(coverage)}
 inspector.replaceChildren(...nodes);
}
function downloadText(name,text,type){const url=URL.createObjectURL(new Blob([text],{type})),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)}
document.getElementById('download-history').onclick=()=>{
 const h=visibleHistory(),rows=['minutes_before_latest,hashrate_hps,temperature_c,power_percent,coverage_percent'];
 h.hashrate.forEach((v,i)=>rows.push([(h.hashrate.length-1-i)*(h.interval_s||300)/60,v,h.temperature_c?.[i]??'',h.power_percent?.[i]??'',h.coverage_percent?.[i]??''].join(',')));
 downloadText('bdlminer-history.csv',rows.join('\n')+'\n','text/csv;charset=utf-8');
};
let latestDiagnostics=null;
async function refreshDiagnostics(){
 if(!document.getElementById('diagnostics').open)return;
 try{
  const d=await request('/api/diagnostics');
  if(!d?.tls||!Array.isArray(d.engines))throw Error(t('Invalid response from the miner.'));
  latestDiagnostics=d;
  const cards=[tile(t('TLS handshakes'),d.tls.handshakes,t('Elapsed time')+': '+dur(d.tls.total_ms/1000)+' · '+d.tls.failures+' '+t('failures'),true)];
  for(const e of d.engines)cards.push(tile(t('Mining worker')+' '+e.index+' · '+e.name,dur(e.hashing_ms/1000),t('Batch time')+'\n'+t('Rest')+': '+dur(e.rest_ms/1000)+' · '+t('Waiting for work')+': '+dur(e.waiting_ms/1000)+' · '+t('Paused')+': '+dur(e.paused_ms/1000),true));
  syncTiles('profile-grid',cards);document.getElementById('profile-status').textContent='';
 }catch(e){document.getElementById('profile-status').textContent=errorText(e)}
}
const diagnosticsPoll=startPolling(refreshDiagnostics,10000);
document.getElementById('diagnostics').addEventListener('toggle',()=>{if(document.getElementById('diagnostics').open)diagnosticsPoll.refresh()});

document.querySelectorAll('[data-range]').forEach(button=>button.onclick=()=>{
 historyRange=Number(button.dataset.range);historySelected=null;
 try{localStorage.setItem('bdl-history-range',String(historyRange))}catch(e){}
 renderHistory();
});
const historyHost=document.getElementById('hashrate-history');historyHost.setAttribute('aria-label',t('Hashrate history'));
document.querySelector('.range-control').setAttribute('aria-label',t('History period'));
document.getElementById('overview-strip').setAttribute('aria-label',t('Device overview'));
historyHost.onkeydown=event=>{
 const count=visibleHistory().hashrate.length;if(!count||!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;
 document.getElementById('chart-inspection').setAttribute('aria-live','polite');
 event.preventDefault();const current=historySelected??count-1;
 selectHistory(event.key==='Home'?0:event.key==='End'?count-1:current+(event.key==='ArrowLeft'?-1:1));
};
historyHost.onblur=()=>selectHistory(null);
let historyResizeFrame=0;
function resizeHistory(){const width=Math.round(document.getElementById('performance-panel').getBoundingClientRect().width);if(width===historyWidth)return;historyWidth=width;if(historyResizeFrame)cancelAnimationFrame(historyResizeFrame);historyResizeFrame=requestAnimationFrame(()=>{historyResizeFrame=0;renderHistory()})}
if(typeof ResizeObserver!=='undefined')new ResizeObserver(resizeHistory).observe(document.getElementById('performance-panel'));else window.addEventListener('resize',resizeHistory);
renderHistory();

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
