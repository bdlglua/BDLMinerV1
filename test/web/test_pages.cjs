// Exercise the actual HTML/JavaScript embedded in the firmware with mocked APIs.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { chromium } = require('playwright');

const source = fs.readFileSync(process.env.BDL_WEB_SOURCE || path.join(__dirname, '../../src/web.cpp'), 'utf8');
const parts = {};
for (const m of source.matchAll(/const char (k\w+)\[\] PROGMEM = R"(\w+)\(([\s\S]*?)\)\2";/g)) parts[m[1]] = m[3];
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
  const state={requests:[],stats:clone(baseStats),posts:[],inflight:0,peak:0,settingsCalls:0,logCalls:0,failLog:!!options.logFailure};
  if(options.stats) Object.assign(state.stats,options.stats);
  const server=http.createServer(async(req,res)=>{
    const chunks=[];for await(const chunk of req)chunks.push(chunk);
    const body=Buffer.concat(chunks).toString();state.requests.push(req.method+' '+req.url);
    if(req.method==='POST')state.posts.push({url:req.url,body});
    if(['/', '/settings', '/log'].includes(req.url)){
      const page=req.url==='/'?'kDashboard':req.url==='/settings'?'kSettingsPage':'kLogPage';
      res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Cache-Control':'no-store'});
      return res.end('<!doctype html><html><head>'+parts.kStyle+"<script>const LANG='"+(options.language||'en')+"';</script>"+parts.kI18n+'</head><body>'+parts.kHeader+parts[page]+'</body></html>');
    }
    if(req.url==='/api/settings'&&req.method==='GET'){
      state.settingsCalls++;
      if(options.settingsDelay)await new Promise(r=>setTimeout(r,options.settingsDelay));
      if(options.settingsFailure&&state.settingsCalls===1)return json(res,{error:'unavailable'},503);
      return json(res,settings);
    }
    if(req.url==='/api/stats'){
      state.peak=Math.max(state.peak,++state.inflight);
      if(options.statsDelay)await new Promise(r=>setTimeout(r,options.statsDelay));
      state.inflight--;
      return json(res,state.stats);
    }
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
      res.writeHead(options.postStatus||200,{'Content-Type':'text/plain'});return res.end(options.postStatus?'Operation refused':'Saved, restarting');
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
  assert.equal(f.state.peak,1);assert.match(await f.page.locator('#m .t').first().textContent(),/0 H\/s/);
}));

check('a stalled response body is also covered by the timeout',()=>run({},async f=>{
  await f.page.goto(f.url);const message=await f.page.evaluate(async()=>{
    try{await request('/slow-body',{timeout:60});return 'no timeout'}catch(e){return e.message}
  });assert.match(message,/timed out/i);
}));

check('peer URLs are normalized and fresh results appear on the first poll',()=>run({stats:{hashrate:1000,peers:['http://peer.test/','peer.test','bad.test']}},async f=>{
  await f.page.route('http://peer.test/api/stats',route=>route.fulfill({json:{hashrate:2000,pool:{connected:true},shares:{accepted:3},best:{ever:1}}}));
  await f.page.route('http://bad.test/api/stats',route=>route.fulfill({json:{broken:true}}));
  await f.page.goto(f.url);await f.page.waitForSelector('#p .t');
  assert.match(await f.page.locator('#p .t').first().textContent(),/3\.00 kH\/s/);
  assert.equal(await f.page.locator('#p .t').count(),3);
  assert.equal(await f.page.locator('#st').textContent(),'ONLINE');
}));

check('GitHub installation stays disabled across dashboard refreshes',()=>run({stats:{github_update:{available:true,version:'1.0.11',installing:true,notes:'45 %'}}},async f=>{
  await f.page.goto(f.url);await f.page.waitForSelector('#m button');
  assert.equal(await f.page.locator('#m button').isDisabled(),true);
  await f.page.waitForTimeout(120);assert.equal(await f.page.locator('#m button').isDisabled(),true);
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
  await f.page.goto(f.url);await f.page.waitForSelector('#m button');await f.page.click('#m button');
  await f.page.waitForTimeout(220);assert.equal(f.state.posts.length,1);
  assert.equal(await f.page.locator('#m button').isDisabled(),true);
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

(async()=>{
  const executablePath=process.env.BDL_CHROMIUM_PATH||(fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined);
  browser=await chromium.launch({headless:true,executablePath,args:['--no-sandbox']});
  let failed=0;
  const selected=cases.filter(c=>!process.env.BDL_WEB_TEST_FILTER||c.name.includes(process.env.BDL_WEB_TEST_FILTER));
  try{for(const c of selected){try{await c.fn();console.log('PASS '+c.name)}catch(e){failed++;console.error('FAIL '+c.name+'\n'+e.stack)}}}
  finally{await browser.close()}
  console.log(`${selected.length-failed}/${selected.length} browser regressions passed`);process.exitCode=failed?1:0;
})().catch(e=>{console.error(e);process.exitCode=1});
