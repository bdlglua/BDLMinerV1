/* Fictional readings for the shipped interfaces. This file never forwards a request. */
(() => {
 'use strict';
 const config=window.BDL_DEMO,query=new URLSearchParams(location.search),fr=query.get('lang')==='fr',gammaView=config.mode==='miner'&&query.get('device')==='gamma';
 const say=(en,french)=>fr?french:en,started=performance.now();
 window.confirm=()=>true;
 const storageKey='bdl-demo-values-'+(gammaView?'gamma':'m5');
 const saved=()=>{try{return JSON.parse(sessionStorage.getItem(storageKey)||'{}')}catch{return{}}};
 const values={coin:0,pool:'public-pool.io',port:23330,addr:'YOUR_BTC_ADDRESS.demo',diff:1,proto:1,sv2key:'0'.repeat(64),uaauto:true,ua:'BDLMinerV2/'+config.version,pool2:'',port2:3333,proto2:0,sv2key2:'',power:100,maxtemp:65,bright:80,cycle:10,nightfrom:255,saver:false,update:1,channel:0,count:false,sharehr:true,sharepool:true,customscreen:false,screen0:1,screen1:2,screen2:4,screen3:3,pages:255,...saved()};
 const state={scene:'mining',powers:{m5:100,gamma:100},channels:{m5:'stable',gamma:'stable'}};
 const json=(body,status=200)=>new Response(JSON.stringify(body),{status,headers:{'Content-Type':'application/json'}});
 const elapsed=()=>Math.floor((performance.now()-started)/1000);
 function history(){
  const h={hashrate:[],temperature_c:[],power_percent:[],coverage_percent:[],interval_s:300,revision:1};
  for(let i=0;i<288;i++){h.hashrate.push(Math.round((gammaView?1.07e12:1002000)*(1+.016*Math.sin(i/9)+.007*Math.cos(i/3))));h.temperature_c.push(Math.round(((gammaView?51:47)+1.5*Math.sin(i/19))*10)/10);h.power_percent.push(100);h.coverage_percent.push(100)}
  return h;
 }
 function stats(){
  const cooling=state.scene==='cooling',connected=state.scene!=='pool',power=cooling?25:Number(values.power),rate=connected?Math.round(1005000*power/100):0,uptime=86400+elapsed();
  const sample={coin:values.coin?'BCH':'BTC',coin_name:values.coin?'Bitcoin Cash':'Bitcoin',algorithm:'SHA-256d',api_version:2,board:'M5StickC Plus2 · '+say('Demo','Démo'),firmware:config.version,firmware_env:'m5stickc-plus2',boot_id:'fictional-demo',uptime_s:uptime,total_hashes:8.65e10+elapsed()*rate,hashrate:rate,hashrate_live:Math.round(rate*(1+.007*Math.sin(elapsed()/3))),
   engines:[{name:'hw',rate:Math.round(rate*.97)},{name:'sw',rate:Math.round(rate*.025)},{name:'fill',rate:Math.round(rate*.005)}],shares:{accepted:20,rejected:0,work:20,rejected_reasons:{}},best:{session:2.84,ever:7.82},blocks:0,over32bits:20,over64bits:0,top:[{d:2.84,t:1793610000}],
   pool:{host:values.pool,port:values.port,connected,status:connected?'Mining':'Disconnected',difficulty:1,protocol:'Stratum V2 (Noise)',user_agent:'BDLMinerV2/'+config.version,connected_percent:99.9,pending_shares:0},
   pool_view:{updated_s:-1,supported:false},pool_diagnostics:{code:connected?'infrequent_shares':'disconnected',compatible:true,connected,work_available:connected,protocol:'Stratum V2 (Noise)',difficulty:1,expected_share_seconds:rate?4294967296/rate:null,share_based_hashrate:994205,accepted_samples:20,estimate_window_s:86400,required_rolling_bits:0,version_rolling_bits:0},
   network:{height:974232,difficulty:1.52e14},market:{updated_s:60,price:78000,currency:'USD',fees:[3,1,1],hashrate:1.04e21,mempool_tx:2000,retarget:{change:-1.3,blocks:1513,progress:25}},
   address:values.addr,address_type:'Demo',lifetime:{hashes:8.65e10,shares:20,blocks:0,seconds:86400},history_revision:1,heap:72432,heap_max_block:53236,temp_c:cooling?68:47,temperature_source:'imu',max_temp_c:65,power_percent:power,power_limit_percent:Number(values.power),throttle:cooling?'hot':'full',limitation_reason:cooling?'hot':power<100?'power':'full',night:false,boot_reason:'Demo',accent:'#f7931a',ip:'192.0.2.40',ssid:'Demo Wi-Fi',rssi:-45,peers:[],firmware_trial:false,
   github_update:{mode:1,checked:true,version:config.version,channel:values.channel?'beta':'stable',available:false}};
  if(gammaView){const gammaRate=connected?1.07e12*power/100:0;Object.assign(sample,{board:'Bitaxe Gamma 601 · '+say('Demo','Démo'),firmware_env:'bitaxe-gamma-601',hashrate:gammaRate,hashrate_live:gammaRate,total_hashes:9.24e15+elapsed()*gammaRate,temp_c:cooling?68:51,temperature_source:'asic',engines:[{name:'BM1370',rate:gammaRate}],shares:{accepted:301,rejected:1,work:2102428,rejected_reasons:{stale:1}},asic:{model:'BM1370',running:true,state:'Mining (experimental)',frequency_mhz:525,profile_mhz:525,fan_rpm:8500,fan_percent:71,fan_auto:true,input_v:5.24,core_v:1.145,current_a:13.7,regulator_temp_c:57,hardware_error_percent:.08}});sample.pool.difficulty=6984.81;sample.pool_diagnostics={...sample.pool_diagnostics,code:connected?'ready':'disconnected',expected_share_seconds:gammaRate?6984.81*4294967296/gammaRate:null,share_based_hashrate:1.04e12}}
  if(values.coin){sample.market.updated_s=-1;sample.network={height:974232,difficulty:5.4e11};sample.pool.protocol='Stratum V1';sample.pool_diagnostics.protocol='Stratum V1'}
  return sample;
 }
 function settings(){
  const schema=[['coin','number','Pool','Cryptocurrency',{min:0,max:1}],['pool','text','Pool','Pool host',{}],['port','number','Pool','Pool port',{min:1,max:65535}],['addr','text','Pool','Bitcoin address',{}],['diff','decimal','Pool','Suggested difficulty',{min:0}],['proto','number','Pool','Protocol',{min:0,max:1}],['sv2key','text','Pool','Authority key',{}],['uaauto','bool','Pool','User agent',{}],['ua','text','Pool','Manual user agent',{}],['pool2','text','Pool','Backup pool host',{}],['port2','number','Pool','Backup pool port',{min:1,max:65535}],['proto2','number','Pool','Backup protocol',{min:0,max:1}],['sv2key2','text','Pool','Backup authority key',{}],['bright','number','Display','Brightness',{min:0,max:100}],['cycle','number','Display','Next screen every',{min:0,max:255}],['power','number','Safety','Mining power',{min:0,max:100}],['maxtemp','number','Safety','Temperature limit',{min:0,max:255}],['saver','bool','Safety','Battery saver',{}],['update','number','Network','Updates',{min:0,max:2}],['channel','number','Network','Update channel',{min:0,max:1}],['count','bool','Network','Usage count',{}]];
  for(const key of ['screen0','screen1','screen2','screen3','pages'])schema.push([key,'number','Display',key,{min:0,max:255}]);
  schema.push(['customscreen','bool','Display','Custom screen',{}],['sharehr','bool','Network','Share my hashrate anonymously',{}],['sharepool','bool','Network','Share my pool anonymously',{}]);
  return {schema,values,live:Object.keys(values),capabilities:{firmware_env:'m5stickc-plus2',display_pages:8,ota:false}};
 }
 function fleet(){
  const m5=stats(),gamma={...stats(),board:'Bitaxe Gamma 601 · '+say('Demo','Démo'),firmware_env:'bitaxe-gamma-601',hashrate:1.07e12*state.powers.gamma/100,hashrate_live:1.07e12*state.powers.gamma/100,temp_c:51,power_percent:state.powers.gamma,power_limit_percent:state.powers.gamma,shares:{accepted:301,rejected:1,work:2102428},asic:{model:'BM1370',running:true,frequency_mhz:525,fan_rpm:8500,regulator_temp_c:57},github_update:{available:false,version:config.version,channel:state.channels.gamma}};
  Object.assign(m5,{power_percent:state.powers.m5,power_limit_percent:state.powers.m5,hashrate:1005000*state.powers.m5/100,github_update:{available:false,version:config.version,channel:state.channels.m5}});
  const url=new URL('../miner/index.html?lang='+(fr?'fr':'en'),location.href).href;
  return {recorded_at:Date.now()/1000,hashrate_hs:m5.hashrate+gamma.hashrate,jobs:{},miners:[{id:'m5',name:'M5StickC Plus2 · '+say('Desk','Bureau'),url,online:true,stale:false,age_s:0,busy:false,sample:m5},{id:'gamma',name:'Gamma 601 · '+say('Workshop','Atelier'),url:url+'&device=gamma',online:true,stale:false,age_s:0,busy:false,sample:gamma},{id:'spare',name:'M5StickC Plus2 · '+say('Spare','Réserve'),url,online:false,stale:true,age_s:7200,busy:false,sample:{...m5,hashrate:0}}]};
 }
 window.fetch=async(input,init={})=>{
  const url=new URL(typeof input==='string'?input:input.url,location.href),method=(init.method||'GET').toUpperCase();
  if(url.origin!==location.origin)throw new TypeError('Demo network access is disabled');
  const path=url.pathname,body=init.body?JSON.parse(init.body):{};
  if(path==='/api/stats')return json(stats());
  if(path==='/api/settings'){
   if(method==='POST'){for(const [key,value]of Object.entries(body))if(Object.hasOwn(values,key))values[key]=value;sessionStorage.setItem(storageKey,JSON.stringify(values));return json({restart:false,changed:Object.keys(body).length})}
   return json(settings());
  }
  if(path==='/api/history'&&config.mode==='hub'){
   const base=url.searchParams.get('miner')==='gamma'?1.07e12:1005000,count=Math.min(2016,Number(url.searchParams.get('hours')||24)*12),now=Date.now()/1000;
   return json({interval_s:300,points:Array.from({length:count},(_,i)=>({t:now-(count-1-i)*300,rate:base*(1+.018*Math.sin(i/8)),boot:'fictional-demo'}))});
  }
  if(path==='/api/history')return json(history());
  if(path==='/api/pool/diagnostics')return json(stats().pool_diagnostics);
  if(path==='/api/diagnostics')return json({uptime_s:stats().uptime_s,firmware:config.version,tls:{handshakes:3,failures:0,total_ms:2400,last_ms:800},engines:[{index:0,name:'hw',hashing_ms:10000,rest_ms:20,waiting_ms:30,paused_ms:0}]});
  if(path==='/api/log')return new Response('12:00:00 [boot] BDLMiner V2 · DEMO\n12:00:01 [wifi] Demo Wi-Fi connected\n12:00:02 [pool] Stratum V2 (Noise) connected\n12:00:30 [stats] 1005 kH/s · fictional demo readings\n',{headers:{'Content-Type':'text/plain'}});
  if(path==='/api/session')return json({token:'fictional-demo-token'});
  if(path==='/api/fleet')return json(fleet());
  if(path==='/api/action'){
   if(body.action==='power'&&Object.hasOwn(state.powers,body.miner)){state.powers[body.miner]=Math.max(0,Math.min(100,Number(body.value)));return json({ok:true})}
   if(body.action==='channel'){state.channels[body.miner]=body.value;return json({ok:true})}
   if(body.action==='diagnose')return json({...stats().pool_diagnostics,protocol:'Stratum V2 (Noise)'});
   if(body.action==='check-update')return json({available:false});
   return json({error:say('This operation requires a real miner.','Cette opération nécessite un vrai miner.')},400);
  }
  if(path==='/api/update-check')return json({available:false});
  return json({error:say('Unavailable in this demo.','Indisponible dans cette démo.')},403);
 };
 window.EventSource=class{
  constructor(){this.readyState=1;this.initial=setTimeout(()=>this.push(),40);this.timer=setInterval(()=>this.push(),2000)}
  push(){if(!document.hidden)this.onmessage?.({data:JSON.stringify(stats())})}
  close(){this.readyState=2;clearTimeout(this.initial);clearInterval(this.timer)}
 };
 if(config.mode==='hub')localStorage.setItem('bdlhub-language',fr?'fr':'en');
 document.addEventListener('DOMContentLoaded',()=>{
  const banner=document.createElement('aside');banner.className='demo-notice';banner.setAttribute('aria-label',say('Demo controls','Commandes de la démo'));
  const label=document.createElement('strong');label.textContent=say('INTERACTIVE DEMO · Fictional data · No miner connected','DÉMO INTERACTIVE · Données fictives · Aucun miner connecté');banner.append(label);
  if(config.mode==='miner'){
   const control=document.createElement('select');control.setAttribute('aria-label',say('Demo scenario','Scénario de démonstration'));
   for(const[value,text]of[['mining',say('Mining','Minage')],['pool',say('Pool disconnected','Pool déconnecté')],['cooling',say('Temperature limit','Limite de température')]]){const option=document.createElement('option');option.value=value;option.textContent=text;control.append(option)}
   control.onchange=()=>{state.scene=control.value;window.dispatchEvent(new Event('online'))};banner.append(control);
  }
  document.body.prepend(banner);
  new ResizeObserver(()=>document.documentElement.style.setProperty('--demo-notice-height',banner.getBoundingClientRect().height+'px')).observe(banner);
  document.addEventListener('click',event=>{const link=event.target.closest('a');if(!link)return;const url=new URL(link.href);if(config.mode==='miner'&&url.origin===location.origin){const mapped={'/settings':'settings.html','/log':'log.html','/':'index.html'}[url.pathname];if(mapped)url.pathname=new URL(mapped,location.href).pathname;if(gammaView)url.searchParams.set('device','gamma')}if(url.origin===location.origin&&url.pathname.endsWith('.html')){url.searchParams.set('lang',fr?'fr':'en');link.href=url.href}} ,true);
  queueMicrotask(()=>{for(const a of document.querySelectorAll('[data-nav]'))if(new URL(a.href).pathname===location.pathname)a.setAttribute('aria-current','page')});
  if(config.mode==='miner')document.querySelector('#maintenance')?.setAttribute('hidden','');
 });
})();
