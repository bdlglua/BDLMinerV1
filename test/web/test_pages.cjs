// Exercise the actual HTML/JavaScript embedded in the firmware with mocked APIs.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { chromium } = require('playwright');

const zlib=require('node:zlib');
const webRoot=path.join(__dirname,'../../web');
const read=name=>fs.readFileSync(path.join(webRoot,name));
const clone = value => JSON.parse(JSON.stringify(value));
const baseStats = {
  board:'Test miner', firmware:'1.0.10', uptime_s:0, hashrate:0, total_hashes:0,
  engines:[{name:'hw',rate:0}], shares:{accepted:0,rejected:0}, best:{session:0,ever:0},
  blocks:0, over32bits:0, top:[], pool:{host:'pool.example',port:3333,connected:true,status:'Mining',difficulty:1},
  pool_view:{updated_s:-1,supported:true}, network:{height:1,difficulty:1e14},
  address:'address.worker', address_type:'P2WPKH', lifetime:{hashes:0,shares:0,blocks:0,seconds:0},
  history:{hashrate:[0,0],interval_s:300}, heap:100000, temp_c:45, throttle:'reduced',
  power_percent:75, night:false, boot_reason:'power on', accent:'#f7931a',
  ip:'192.168.1.20', ssid:'WiFi',rssi:-45, peers:[], market:{updated_s:-1}, github_update:{available:false}
};
const settings = {
  schema:[
    ['pool','text','Pool','Pool host',{}], ['port','number','Pool','Pool port',{min:1,max:65535}],
    ['addr','text','Pool','BTC address',{}], ['diff','decimal','Pool','Suggested difficulty',{min:0}],
    ['power','number','Safety','Mining power',{min:0,max:100}],
    ['maxtemp','number','Safety','Temperature limit',{min:0,max:255}],
    ['nightfrom','number','Display','Night hour',{min:0,max:255}],
    ['saver','bool','Safety','Battery saver',{}], ['webpass','secret','Network','Web password',{}]
  ],
  values:{pool:'pool.example',port:3333,addr:'address.worker',diff:0.0002,power:75,maxtemp:65,nightfrom:255,saver:false,webpass:'password'}
};
let browser;
const cases=[];
function check(name,fn){ cases.push({name,fn}); }
function json(res,value,status=200){res.writeHead(status,{'Content-Type':'application/json'});res.end(JSON.stringify(value));}

async function fixture(options={}){
  const state={settings:clone(settings),postStatus:options.postStatus||200,postText:options.postText||null,requests:[],stats:clone(baseStats),posts:[],inflight:0,peak:0,settingsCalls:0,logCalls:0,failLog:!!options.logFailure,historyCalls:0,history:options.history||{hashrate:[1000000,900000],temperature_c:[45,55],power_percent:[100,50],coverage_percent:[100,100],interval_s:300,revision:1},diagnostics:options.diagnostics||{uptime_s:0,firmware:'test',tls:{handshakes:2,failures:0,total_ms:1000,last_ms:500},engines:[{index:0,name:'hw',hashing_ms:10000,rest_ms:20,waiting_ms:30,paused_ms:40}]}};
  if(options.stats) Object.assign(state.stats,options.stats);
  if(options.capabilities)state.settings.capabilities=options.capabilities;
  const server=http.createServer(async(req,res)=>{
    const chunks=[];for await(const chunk of req)chunks.push(chunk);
    const body=Buffer.concat(chunks).toString();state.requests.push(req.method+' '+req.url);
    if(req.method==='POST')state.posts.push({url:req.url,body});
    if(['/', '/settings', '/log'].includes(req.url)){
      const page=req.url==='/'?'dashboard':req.url==='/settings'?'settings':'log';
      res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Cache-Control':'no-store'});
      return res.end('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="stylesheet" href="/assets/style.css">'+"<script>const LANG='"+(options.language||'en')+"';</script>"+'<script defer src="/assets/common.js"></script><script defer src="/assets/'+page+'.js"></script></head><body>'+read('header.html')+read(page+'.html')+'</div></div></body></html>');
    }
    if(req.url.startsWith('/assets/')){
      const name=req.url.slice('/assets/'.length).split('?')[0];
      if(!['style.css','common.js','dashboard.js','settings.js','log.js'].includes(name)){res.writeHead(404);return res.end()}
      res.writeHead(200,{'Content-Type':name.endsWith('.css')?'text/css':'text/javascript','Content-Encoding':'gzip'});
      return res.end(zlib.gzipSync(read(name)));
    }
    if(req.url==='/api/settings'&&req.method==='GET'){
      state.settingsCalls++;
      if(options.settingsDelay)await new Promise(r=>setTimeout(r,options.settingsDelay));
      if(options.settingsFailure&&state.settingsCalls===1)return json(res,{error:'unavailable'},503);
      if(options.settingsDenied){res.writeHead(403);return res.end('Set a web password in the setup portal first (hold the main button while the miner starts).')}
      return json(res,state.settings);
    }
    if(req.url.split('?')[0]==='/api/stats'){
      state.peak=Math.max(state.peak,++state.inflight);
      if(options.statsDelay)await new Promise(r=>setTimeout(r,options.statsDelay));
      state.inflight--;
      return json(res,state.stats);
    }
    if(req.url==='/api/history'){state.historyCalls++;if(options.historyFailure){res.writeHead(503);return res.end('History unavailable')}return json(res,state.history)}
    if(req.url==='/api/diagnostics'){return json(res,{...state.diagnostics,uptime_s:state.stats.uptime_s})}
    if(req.url==='/api/log'){
      state.logCalls++;
      if(state.failLog){res.writeHead(500);return res.end('Log unavailable')}
      return res.end('Miner log is available');
    }
    if(req.url==='/slow-body'){
      res.writeHead(200,{'Content-Type':'application/json'});res.write('{');
      return setTimeout(()=>res.end('"ok":true}'),500);
    }
    if(req.method==='POST'){
      if(options.postDelay)await new Promise(r=>setTimeout(r,options.postDelay));
      res.writeHead(state.postStatus,{'Content-Type':'text/plain'});return res.end(state.postText||(state.postStatus>=400?'Operation refused':'Saved, restarting'));
    }
    res.writeHead(404);res.end('Not found');
  });
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const context=await browser.newContext({viewport:options.mobile?{width:375,height:812}:{width:1100,height:800}});
  const page=await context.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  // Speed up polling without changing request timeouts or server timing.
  await page.addInitScript(()=>{
    const timeout=window.setTimeout.bind(window),interval=window.setInterval.bind(window);
    window.setTimeout=(fn,ms,...args)=>timeout(fn,ms===3000?30:ms===5000?50:ms,...args);
    window.setInterval=(fn,ms,...args)=>interval(fn,ms===3000?30:ms===5000?50:ms,...args);
  });
  return {page,state,errors,url:'http://127.0.0.1:'+server.address().port,
    async close(){await context.close();server.closeAllConnections();await new Promise(r=>server.close(r));}};
}
async function loaded(f){await f.page.goto(f.url+'/settings');await f.page.waitForSelector('#k_pool');}
async function run(options,fn){const f=await fixture(options);try{await fn(f);assert.deepEqual(f.errors,[],'uncaught browser errors')}finally{await f.close()}}

check('settings cannot submit before load, and failed loads can be retried',()=>run({settingsDelay:150,settingsFailure:true},async f=>{
  await f.page.goto(f.url+'/settings');
  assert.equal(await f.page.locator('#save').isDisabled(),true);
  await f.page.waitForSelector('#retry:not([hidden])');await f.page.click('#retry');
  await f.page.waitForSelector('#k_pool');assert.equal(await f.page.locator('#save').isEnabled(),true);
}));

check('blank and out-of-range numeric fields never become zero-valued settings',()=>run({},async f=>{
  await loaded(f);await f.page.fill('#k_maxtemp','');await f.page.click('#save');
  await f.page.waitForTimeout(80);assert.equal(f.state.posts.length,0);
  await f.page.fill('#k_maxtemp','65');await f.page.fill('#k_power','101');await f.page.click('#save');
  await f.page.waitForTimeout(80);assert.equal(f.state.posts.length,0);
  await f.page.fill('#k_power','75');await f.page.fill('#k_diff','0');await f.page.click('#save');
  await f.page.waitForTimeout(80);assert.equal(f.state.posts.length,0);
}));

check('Enter submits once, and actions remain locked until restart',()=>run({postDelay:100},async f=>{
  await loaded(f);await f.page.fill('#k_pool','new.pool');await f.page.press('#k_pool','Enter');
  await f.page.waitForFunction(()=>document.querySelector('#save').disabled);
  await f.page.waitForTimeout(150);assert.equal(f.state.posts.length,1);
  assert.equal(JSON.parse(f.state.posts[0].body).pool,'new.pool');
  assert.equal(await f.page.locator('#restore').isDisabled(),true);
  assert.match(await f.page.locator('#msg').textContent(),/restarting/);
}));

check('failed saves display an error and permit retry',()=>run({postStatus:500},async f=>{
  await loaded(f);await f.page.click('#save');
  await f.page.waitForFunction(()=>document.querySelector('#msg').textContent.includes('refused'));
  assert.equal(await f.page.locator('#save').isEnabled(),true);
}));

check('polling never overlaps slow requests, and zero hashrate is displayed',()=>run({statsDelay:140},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('#m .t');await f.page.waitForTimeout(450);
  assert.equal(f.state.peak,1);
  const rate=f.page.locator('#m .t').filter({has:f.page.locator('.l').filter({hasText:/^Hashrate$/})});
  assert.match(await rate.textContent(),/0 H\/s/);
}));

check('a stalled response body is also covered by the timeout',()=>run({},async f=>{
  await f.page.goto(f.url);const message=await f.page.evaluate(async()=>{
    try{await request('/slow-body',{timeout:60});return 'no timeout'}catch(e){return e.message}
  });assert.match(message,/timed out/i);
}));

check('peer URLs are normalized and fresh results appear on the first poll',()=>run({stats:{hashrate:1000,peers:['http://peer.test/','peer.test','bad.test']}},async f=>{
  await f.page.route('http://peer.test/api/stats?history=0',route=>route.fulfill({json:{hashrate:2000,pool:{connected:true},shares:{accepted:3},best:{ever:1}}}));
  await f.page.route('http://bad.test/api/stats?history=0',route=>route.fulfill({json:{broken:true}}));
  await f.page.goto(f.url);await f.page.waitForSelector('#p .t');
  assert.match(await f.page.locator('#p .t').first().textContent(),/3\.00 kH\/s/);
  assert.equal(await f.page.locator('#p .t').count(),3);
  assert.equal(await f.page.locator('#st').textContent(),'ONLINE');
}));

check('GitHub installation stays disabled across dashboard refreshes',()=>run({stats:{github_update:{available:true,version:'1.0.11',installing:true,notes:'45 %'}}},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('.gh-install');
  assert.equal(await f.page.locator('.gh-install').isDisabled(),true);
  await f.page.waitForTimeout(120);assert.equal(await f.page.locator('.gh-install').isDisabled(),true);
  assert.equal(f.state.posts.length,0);
}));

check('backup restore preserves uint64 integers and cancellation is harmless',()=>run({},async f=>{
  await loaded(f);
  await f.page.evaluate(()=>imp({files:[]}));assert.equal(f.state.posts.length,0);
  f.page.on('dialog',dialog=>dialog.accept());
  const raw='{"bdlminer_backup":1,"settings":{},"stats":{"hashes":18446744073709551615}}';
  await f.page.setInputFiles('#imp',{name:'backup.json',mimeType:'application/json',buffer:Buffer.from(raw)});
  await f.page.waitForFunction(()=>document.querySelector('#msg').textContent.includes('restored'));
  assert.equal(f.state.posts[0].body,raw);assert.equal(await f.page.locator('#save').isDisabled(),true);
}));

check('an accepted install request cannot be submitted again while waiting',()=>run({stats:{github_update:{available:true,version:'1.0.11',installing:false,notes:''}},postDelay:100},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('.gh-install');await f.page.click('.gh-install');
  await f.page.waitForTimeout(220);assert.equal(f.state.posts.length,1);
  assert.equal(await f.page.locator('.gh-install').isDisabled(),true);
}));

check('GitHub errors stay visible when no update is available and a manual check is queued once',()=>run({language:'fr',stats:{github_update:{mode:1,checked:false,available:false,error:'release unavailable: HTTP 403'}},postDelay:100},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('.gh-check');
  assert.match(await f.page.locator('#updates').textContent(),/HTTP 403/);
  assert.equal(await f.page.locator('.gh-check').textContent(),'Vérifier maintenant');
  await f.page.click('.gh-check');await f.page.waitForTimeout(60);
  assert.equal(f.state.posts.length,1);assert.equal(f.state.posts[0].url,'/api/github-check');
  assert.equal(await f.page.locator('.gh-check').isDisabled(),true);
}));

check('an installation failing before the next poll can be retried',()=>run({stats:{github_update:{available:true,version:'1.0.11',request_id:0}}},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('.gh-install');await f.page.click('.gh-install');
  await f.page.waitForTimeout(80);
  f.state.stats.github_update={available:true,version:'1.0.11',request_id:1,installing:false,error:'this board requires USB'};
  await f.page.waitForFunction(()=>!document.querySelector('.gh-install').disabled);
  assert.match(await f.page.locator('#updates').textContent(),/requires USB/);
}));

check('disabled updates and pending checks are shown on the dashboard',()=>run({stats:{github_update:{mode:0,available:false,checked:false}}},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('.gh-check');
  assert.equal(await f.page.locator('.gh-check').isDisabled(),true);
  assert.match(await f.page.locator('#updates').textContent(),/disabled in settings/);
  f.state.stats.github_update={mode:1,checking:true,available:false};
  await f.page.waitForFunction(()=>document.querySelector('.gh-check').textContent==='Checking…');
  assert.equal(await f.page.locator('.gh-check').isDisabled(),true);
}));

check('invalid backup files produce a translated message and can be retried',()=>run({language:'fr'},async f=>{
  await loaded(f);await f.page.setInputFiles('#imp',{name:'bad.json',mimeType:'application/json',buffer:Buffer.from('[]')});
  await f.page.waitForFunction(()=>document.querySelector('#msg').textContent.includes('sauvegarde'));
  assert.equal(f.state.posts.length,0);assert.equal(await f.page.locator('#restore').isEnabled(),true);
  assert.equal(await f.page.locator('#imp').inputValue(),'');
}));

check('factory images are rejected before upload; server failures allow retry',()=>run({postStatus:500},async f=>{
  await loaded(f);const buffer=Buffer.from([0xe9,1,2,3]);
  await f.page.setInputFiles('#fw',{name:'board-factory.bin',mimeType:'application/octet-stream',buffer});
  await f.page.click('#upload');await f.page.waitForTimeout(80);assert.equal(f.state.posts.length,0);
  await f.page.setInputFiles('#fw',{name:'board-firmware.bin',mimeType:'application/octet-stream',buffer});
  await f.page.click('#upload');await f.page.waitForFunction(()=>document.querySelector('#up').textContent.includes('refused'));
  assert.equal(f.state.posts.length,1);assert.equal(await f.page.locator('#upload').isEnabled(),true);
}));

check('log errors are visible and polling recovers',()=>run({logFailure:true},async f=>{
  await f.page.goto(f.url+'/log');await f.page.waitForFunction(()=>document.querySelector('#log-status').textContent.length>0);
  f.state.failLog=false;
  await f.page.waitForFunction(()=>document.querySelector('#l').textContent.includes('available'));
  assert.equal(await f.page.locator('#log-status').textContent(),'');
}));

check('settings stay within a mobile viewport and labels focus their inputs',()=>run({mobile:true,language:'fr'},async f=>{
  await loaded(f);assert.equal(await f.page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  await f.page.locator('label[for="k_power"]').click();assert.equal(await f.page.evaluate(()=>document.activeElement.id),'k_power');
}));


check('full performance is an explicit 100 percent choice and preserves thermal protection',()=>run({language:'fr'},async f=>{
  await loaded(f);await f.page.fill('#settings-search','puissance');
  assert.equal(await f.page.locator('#k_pool').isVisible(),false);
  await f.page.click('#preset-100');assert.equal(await f.page.locator('#k_power').inputValue(),'100');
  assert.equal(await f.page.locator('#k_maxtemp').inputValue(),'65');assert.equal(f.state.posts.length,0);
  await f.page.click('#save');await f.page.waitForFunction(()=>document.querySelector('#msg').textContent.includes('redémarrage'));
  const saved=JSON.parse(f.state.posts[0].body);assert.equal(saved.power,100);assert.equal(saved.maxtemp,65);
}));

check('secret visibility toggles do not submit settings',()=>run({},async f=>{
  await loaded(f);const show=f.page.getByRole('button',{name:'Show Web password',exact:true});await show.click();
  assert.equal(await f.page.locator('#k_webpass').getAttribute('type'),'text');assert.equal(f.state.posts.length,0);
  await f.page.getByRole('button',{name:'Hide Web password',exact:true}).click();
  assert.equal(await f.page.locator('#k_webpass').getAttribute('type'),'password');
}));

check('settings navigation restores filtered groups and marks the active page',()=>run({},async f=>{
  await loaded(f);await f.page.fill('#settings-search','no-matching-setting');
  assert.equal(await f.page.locator('#settings-empty').isVisible(),true);
  await f.page.locator('#settings-nav a').first().click();assert.equal(await f.page.locator('#k_pool').isVisible(),true);
  assert.equal(await f.page.locator('[data-nav="/settings"]').getAttribute('aria-current'),'page');
}));

check('read-only setup errors stay inside the translated interface',()=>run({language:'fr',settingsDenied:true},async f=>{
  await f.page.goto(f.url+'/settings');await f.page.waitForFunction(()=>document.querySelector('#msg').textContent.includes('mot de passe'));
  assert.equal(await f.page.locator('#save').isDisabled(),true);assert.equal(await f.page.locator('h1').textContent(),'Réglages et maintenance');
}));

check('USB-only boards explain the update path and prevent web installs',()=>run({capabilities:{ota:false},stats:{github_update:{available:true,supported:false,version:'1.1.0',release_url:'https://github.com/bdlglua/BDLMinerV1/releases/tag/v1.1.0'}}},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('#updates a');assert.equal(await f.page.locator('.gh-install').count(),0);
  assert.match(await f.page.locator('#updates a').getAttribute('href'),/v1\.1\.0/);
  await loaded(f);await f.page.setInputFiles('#fw',{name:'board-firmware.bin',mimeType:'application/octet-stream',buffer:Buffer.from([0xe9,1,2,3])});
  assert.equal(await f.page.locator('#upload').isDisabled(),true);assert.match(await f.page.locator('#firmware-help').textContent(),/USB/);
}));

check('device diagnostics distinguish heat limits from configured power',()=>run({stats:{limitation_reason:'thermal',max_temp_c:65,power_limit_percent:100,power_percent:50}},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('#health:not([hidden])');assert.match(await f.page.locator('#health').textContent(),/Temperature protection/);
  assert.match(await f.page.locator('#device-grid').textContent(),/100 %/);
  f.state.stats.limitation_reason='full';f.state.stats.power_percent=100;
  await f.page.waitForFunction(()=>document.querySelector('#health').hidden);
}));

check('hidden tabs stop polling and visibility resumes without overlap',()=>run({statsDelay:50},async f=>{
  await f.page.addInitScript(()=>{window.__hidden=false;Object.defineProperty(document,'hidden',{get:()=>window.__hidden})});
  await f.page.goto(f.url);await f.page.waitForSelector('#m .t');
  await f.page.evaluate(()=>{window.__hidden=true;document.dispatchEvent(new Event('visibilitychange'))});
  await f.page.waitForTimeout(100);const paused=f.state.requests.filter(x=>x.startsWith('GET /api/stats')).length;
  await f.page.waitForTimeout(100);assert.equal(f.state.requests.filter(x=>x.startsWith('GET /api/stats')).length,paused);
  await f.page.evaluate(()=>{window.__hidden=false;document.dispatchEvent(new Event('visibilitychange'));window.dispatchEvent(new Event('online'))});
  await f.page.waitForTimeout(120);assert.ok(f.state.requests.filter(x=>x.startsWith('GET /api/stats')).length>paused);assert.equal(f.state.peak,1);
}));

check('log filtering and pause leave the original text available for download',()=>run({},async f=>{
  await f.page.goto(f.url+'/log');await f.page.waitForFunction(()=>document.querySelector('#l').textContent.includes('available'));
  await f.page.fill('#log-search','no-matching-line');assert.equal(await f.page.locator('#l').textContent(),'No matching entries.');
  await f.page.click('#pause-log');await f.page.waitForTimeout(80);const calls=f.state.logCalls;await f.page.waitForTimeout(100);assert.equal(f.state.logCalls,calls);
  const download=f.page.waitForEvent('download');await f.page.click('#download-log');const file=await download;assert.equal(file.suggestedFilename(),'bdlminer.log');
  await f.page.fill('#log-search','');assert.match(await f.page.locator('#l').textContent(),/available/);
  await f.page.click('#pause-log');await f.page.waitForTimeout(80);assert.ok(f.state.logCalls>calls);
}));

check('restart cancellation is harmless and accepted restarts lock actions',()=>run({},async f=>{
  await loaded(f);f.page.once('dialog',dialog=>dialog.dismiss());await f.page.click('#restart');assert.equal(f.state.posts.length,0);
  f.page.once('dialog',dialog=>dialog.accept());await f.page.click('#restart');
  await f.page.waitForFunction(()=>document.querySelector('#msg').textContent.includes('Restarting'));
  assert.equal(f.state.posts[0].url,'/api/restart');assert.equal(await f.page.locator('#save').isDisabled(),true);
}));

check('mobile dashboard and log remain within the viewport',()=>run({mobile:true,language:'fr'},async f=>{
  for(const page of ['/','/log']){await f.page.goto(f.url+page);await f.page.waitForTimeout(100);assert.equal(await f.page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true)}
}));


check('light polls fetch history only when its revision changes',()=>run({stats:{history:undefined,history_revision:1}},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('#history-grid .t');
  assert.equal(f.state.historyCalls,1);await f.page.waitForTimeout(150);assert.equal(f.state.historyCalls,1);
  assert.ok(f.state.requests.some(x=>x==='GET /api/stats?history=0'));
  f.state.history={...f.state.history,revision:2};f.state.stats.history_revision=2;
  await f.page.waitForTimeout(120);assert.equal(f.state.historyCalls,2);
}));
check('history failures do not mark mining offline and do not hammer the endpoint',()=>run({stats:{history:undefined,history_revision:1},historyFailure:true},async f=>{
  await f.page.goto(f.url);await f.page.waitForFunction(()=>document.querySelector('#history-status').textContent.length>0);
  await f.page.waitForTimeout(160);assert.equal(f.state.historyCalls,1);assert.equal(await f.page.locator('#st').textContent(),'ONLINE');
}));
check('history CSV retains sensor gaps and power limits',()=>run({stats:{history:undefined,history_revision:1},history:{hashrate:[1000000,500000],temperature_c:[null,60],power_percent:[100,50],coverage_percent:[100,98],interval_s:300,revision:1}},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('#history-grid .t');
  const waiting=f.page.waitForEvent('download');await f.page.click('#download-history');const download=await waiting;
  const stream=await download.createReadStream();let text='';for await(const chunk of stream)text+=chunk.toString();
  assert.match(text,/5,1000000,,100,100/);assert.match(text,/0,500000,60,50,98/);
}));
check('diagnostics show worker wall times and TLS counts',()=>run({},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('#profile-grid .t');assert.match(await f.page.locator('#profile-grid').textContent(),/TLS handshakes/);
  assert.match(await f.page.locator('#profile-grid').textContent(),/Mining worker 0/);
}));
check('failed flash saves stay translated and allow another attempt',()=>run({language:'fr',postStatus:500,postText:'Settings could not be saved. Previous configuration kept; try again.'},async f=>{
  await loaded(f);await f.page.click('#save');await f.page.waitForFunction(()=>document.querySelector('#msg').textContent.includes('configuration précédente'));
  assert.equal(await f.page.locator('#save').isEnabled(),true);assert.equal(await f.page.locator('#restart').isEnabled(),true);
  f.state.postStatus=200;f.state.postText=null;await f.page.click('#save');await f.page.waitForFunction(()=>document.querySelector('#msg').textContent.includes('redémarrage'));assert.equal(f.state.posts.length,2);assert.equal(await f.page.locator('#save').isDisabled(),true);
}));
check('GitHub download retries are visible and can be requested immediately',()=>run({language:'fr',stats:{github_update:{available:true,mode:2,version:'1.2.0',retry_in_s:300,error:'download failed'}}},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('.gh-install');assert.equal(await f.page.locator('.gh-install').textContent(),'Réessayer maintenant');
  assert.match(await f.page.locator('#updates').textContent(),/Prochaine tentative dans 5m/);
}));
check('measurement uses actual hash deltas and exports no settings secrets',()=>run({},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('#start-benchmark:enabled');await f.page.click('#start-benchmark');
  await f.page.waitForSelector('#stop-benchmark:enabled');f.state.stats.uptime_s=300;f.state.stats.total_hashes=300000000;f.state.stats.temp_c=60;f.state.stats.power_percent=50;
  await f.page.waitForFunction(()=>document.querySelector('#benchmark-status').textContent.includes('5m'));
  await f.page.click('#stop-benchmark');await f.page.waitForSelector('#download-benchmark:enabled');
  const waiting=f.page.waitForEvent('download');await f.page.click('#download-benchmark');const download=await waiting;
  const stream=await download.createReadStream();let text='';for await(const chunk of stream)text+=chunk.toString();const report=JSON.parse(text);
  assert.equal(report.hashrate_hps,1000000);assert.equal(report.elapsed_s,300);assert.equal(report.temperature_max_c,60);assert.equal(report.power_min_percent,50);
  assert.equal(report.sample_gaps,true);assert.ok(report.profiling_start&&report.profiling_end);assert.equal(text.includes('password'),false);
}));
check('measurement is cancelled by a restart or a hidden tab',()=>run({},async f=>{
  f.state.stats.uptime_s=100;f.state.stats.total_hashes=100000000;
  await f.page.addInitScript(()=>{window.__hidden=false;Object.defineProperty(document,'hidden',{get:()=>window.__hidden})});
  await f.page.goto(f.url);await f.page.waitForSelector('#start-benchmark:enabled');await f.page.click('#start-benchmark');await f.page.waitForSelector('#stop-benchmark:enabled');
  f.state.stats.uptime_s=2;f.state.stats.total_hashes=1000;await f.page.waitForFunction(()=>document.querySelector('#benchmark-status').textContent.includes('restarted'));
  assert.equal(await f.page.locator('#download-benchmark').isDisabled(),true);
  await f.page.click('#start-benchmark');await f.page.waitForSelector('#stop-benchmark:enabled');
  await f.page.evaluate(()=>{window.__hidden=true;document.dispatchEvent(new Event('visibilitychange'))});
  assert.match(await f.page.locator('#benchmark-status').textContent(),/cancelled/);assert.equal(await f.page.locator('#stop-benchmark').isDisabled(),true);
}));


check('measurement automatically finishes after ten minutes of device uptime',()=>run({},async f=>{
  f.state.stats.uptime_s=100;f.state.stats.total_hashes=100000000;
  await f.page.goto(f.url);await f.page.waitForSelector('#start-benchmark:enabled');await f.page.click('#start-benchmark');await f.page.waitForSelector('#stop-benchmark:enabled');
  f.state.stats.uptime_s=700;f.state.stats.total_hashes=700000000;
  await f.page.waitForSelector('#download-benchmark:enabled');assert.match(await f.page.locator('#benchmark-status').textContent(),/1\.00 MH\/s/);
  assert.equal(await f.page.locator('#stop-benchmark').isDisabled(),true);
}));
check('boot identity invalidates history and measurements even with higher counters',()=>run({stats:{history:undefined,history_revision:1,boot_id:'old',uptime_s:100,total_hashes:100000000}},async f=>{
  f.state.diagnostics.boot_id='old';await f.page.goto(f.url);await f.page.waitForSelector('#history-grid .t');
  await f.page.click('#start-benchmark');await f.page.waitForSelector('#stop-benchmark:enabled');
  f.state.stats.boot_id='new';f.state.stats.uptime_s=200;f.state.stats.total_hashes=200000000;
  await f.page.waitForFunction(()=>document.querySelector('#benchmark-status').textContent.includes('restarted'));
  assert.equal(await f.page.locator('#download-benchmark').isDisabled(),true);assert.equal(f.state.historyCalls,2);
}));

if(process.env.BDL_SCREENSHOT_DIR){
 fs.mkdirSync(process.env.BDL_SCREENSHOT_DIR,{recursive:true});
 for(const [name,options,route]of [['dashboard',{language:'fr'},'/'],['mobile',{mobile:true,language:'fr'},'/'],['settings',{language:'fr'},'/settings'],['log',{language:'fr'},'/log']]){
  check('visual preview '+name,()=>run({...options,stats:{hashrate:1001240,hashrate_live:1004820,engines:[{name:'hw',rate:968500},{name:'sw',rate:27000},{name:'fill',rate:5740}],power_percent:100,power_limit_percent:100,throttle:'full',limitation_reason:'full',max_temp_c:65,uptime_s:18432,shares:{accepted:247,rejected:1},best:{session:0.0034,ever:1.28},history:undefined,history_revision:1,boot_id:'preview',github_update:{mode:2,checked:true,version:'1.1.0',available:false},board:'M5StickC Plus2'}},async f=>{
   await f.page.goto(f.url+route);await f.page.waitForTimeout(200);
   await f.page.screenshot({path:path.join(process.env.BDL_SCREENSHOT_DIR,name+'.png'),fullPage:true});
  }));
 }
}

(async()=>{
  const executablePath=process.env.BDL_CHROMIUM_PATH||(fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined);
  browser=await chromium.launch({headless:true,executablePath,args:['--no-sandbox']});
  let failed=0;
  const selected=cases.filter(c=>!process.env.BDL_WEB_TEST_FILTER||c.name.includes(process.env.BDL_WEB_TEST_FILTER));
  try{for(const c of selected){try{await c.fn();console.log('PASS '+c.name)}catch(e){failed++;console.error('FAIL '+c.name+'\n'+e.stack)}}}
  finally{await browser.close()}
  console.log(`${selected.length-failed}/${selected.length} browser regressions passed`);process.exitCode=failed?1:0;
})().catch(e=>{console.error(e);process.exitCode=1});
