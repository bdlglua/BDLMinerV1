// Exercise the real Python hub and shipped browser assets against loopback fixtures.
const assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const fs=require('node:fs');
const path=require('node:path');
const {chromium}=require('playwright');
async function main(){
 const child=spawn('python3',[path.join(__dirname,'hub_fixture.py')],{stdio:['ignore','pipe','pipe']});let errors='';child.stderr.on('data',d=>errors+=d);
 const url=await new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(Error('Fixture startup timed out: '+errors)),15000);child.stdout.once('data',d=>{clearTimeout(timeout);resolve(d.toString().trim())});child.once('exit',code=>{clearTimeout(timeout);reject(Error('Fixture exited '+code+': '+errors))})});
 let browser;
 try{
  const executablePath=process.env.BDL_CHROMIUM_PATH||(fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined);
  browser=await chromium.launch({headless:true,executablePath,args:['--no-sandbox']});
  const page=await browser.newPage({viewport:{width:1440,height:1000}}),failures=[];page.on('pageerror',e=>failures.push(e.message));
  await page.goto(url);await page.waitForSelector('.miner');
  assert.equal(await page.locator('.miner').count(),2);assert.match(await page.locator('#total').textContent(),/1 T/);
  assert.match(await page.locator('.miner.offline').textContent(),/Offline/);assert.doesNotMatch(await page.locator('body').textContent(),/HIDDEN/);
  const gamma=page.locator('[data-id=gamma]');assert.equal(await gamma.locator('select').inputValue(),'beta');
  await gamma.getByRole('button',{name:'Diagnose pool'}).click();await page.waitForFunction(()=>document.querySelector('.result').textContent.includes('Stratum V2'));
  assert.match(await gamma.locator('.result').textContent(),/43 s/);
  await gamma.locator('input').fill('75');await gamma.getByRole('button',{name:'Apply',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('.result').textContent.includes('Operation requested'));
  await page.selectOption('#lang','fr');assert.match(await page.locator('h1').textContent(),/Tous vos miners/);
  assert.match(await gamma.textContent(),/Température/);assert.match(await page.locator('.miner.offline').textContent(),/Hors ligne/);
  await page.waitForFunction(()=>document.querySelector('#history-info').textContent.includes('interruptions'));
  assert.match(await page.locator('body').textContent(),/Rétab|rétablit/);
  if(process.env.BDL_SCREENSHOT_DIR){fs.mkdirSync(process.env.BDL_SCREENSHOT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.BDL_SCREENSHOT_DIR,'v2-fleet-fixture.png'),fullPage:true})}
  await page.setViewportSize({width:375,height:812});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  await page.selectOption('#hours','1');await page.waitForTimeout(100);
  await page.selectOption('#lang','en');await page.evaluate(()=>clearTimeout(timer));
  await gamma.locator('input').fill('63');await page.locator('h1').click();await page.evaluate(()=>refresh(false));
  assert.equal(await gamma.locator('input').inputValue(),'63','an unfocused power edit must survive polling');
  const applied=page.waitForResponse(r=>r.url().endsWith('/api/action')&&r.request().postDataJSON()?.value===63);
  await gamma.getByRole('button',{name:'Apply',exact:true}).click();await applied;
  await page.waitForFunction(()=>lastFleet.miners.find(m=>m.id==='gamma').sample.power_limit_percent===63);
  assert.equal(await gamma.locator('input').inputValue(),'63','input is the configured limit, not effective throttled power');
  assert.match(await gamma.textContent(),/Effective power/);

  let releaseHistory,historyStarted;const startedHistory=new Promise(r=>historyStarted=r);
  await page.route('**/api/history?**',async route=>{
   const hours=new URL(route.request().url()).searchParams.get('hours');
   if(hours==='6'){historyStarted();await new Promise(r=>releaseHistory=r)}
   await route.fulfill({json:{interval_s:60,points:[{t:Date.now()/1000,rate:hours==='6'?6e12:1e12,boot:'fixture'}]}});
  });
  await page.selectOption('#hours','6');await startedHistory;await page.selectOption('#hours','1');
  await page.waitForFunction(()=>historyData.points[0]?.rate===1e12);
  const olderHistory=page.waitForResponse(r=>r.url().includes('/api/history?')&&new URL(r.url()).searchParams.get('hours')==='6');
  releaseHistory();await olderHistory;await page.waitForTimeout(50);
  assert.equal(await page.evaluate(()=>historyData.points[0].rate),1e12,'late old chart data must not overwrite the selected window');
  await page.unroute('**/api/history?**');

  await page.route('**/api/fleet',route=>route.abort());await page.evaluate(()=>refresh(false));
  assert.equal(await page.locator('#total').textContent(),'—');assert.match(await page.locator('#notice').textContent(),/last known readings/);
  assert.equal(await gamma.getByRole('button',{name:'Apply',exact:true}).isDisabled(),true);
  await page.unroute('**/api/fleet');await page.evaluate(()=>refresh(false));
  assert.equal(await gamma.getByRole('button',{name:'Apply',exact:true}).isEnabled(),true);

  let releaseFleet,startedFleet,count=0;const fleetStarted=new Promise(r=>startedFleet=r);
  const snapshot=await (await page.request.get(url+'/api/fleet')).json();
  await page.route('**/api/fleet',async route=>{
   const data=structuredClone(snapshot);data.hashrate_hs=++count===1?1e12:2e12;
   if(count===1){startedFleet();await new Promise(r=>releaseFleet=r)}
   await route.fulfill({json:data});
  });
  await page.evaluate(()=>{void refresh(false)});await fleetStarted;await page.evaluate(()=>refresh(false));
  assert.match(await page.locator('#total').textContent(),/2 T/);
  const olderFleet=page.waitForResponse(r=>r.url().endsWith('/api/fleet'));releaseFleet();await olderFleet;await page.waitForTimeout(50);
  assert.match(await page.locator('#total').textContent(),/2 T/,'an older fleet request must not rewind the overview');
  await page.unroute('**/api/fleet');
  let commands=0,received=null;
  await page.route('**/api/session',route=>route.fulfill({json:{token:'replacement-fixture-token'}}));
  await page.route('**/api/action',async route=>{
   commands++;received={header:route.request().headers().authorization,body:route.request().postDataJSON()};
   await route.fulfill(commands===1?{status:401,json:{error:'token expired'}}:{json:{ok:true}});
  });
  await page.evaluate(()=>api('/api/action',{miner:'gamma',action:'power',value:42}));
  assert.equal(commands,2);assert.equal(received.header,'Bearer replacement-fixture-token');assert.equal(received.body.value,42);
  await page.unroute('**/api/action');commands=0;
  await page.route('**/api/action',route=>{commands++;return route.fulfill({status:500,json:{error:'fixture failure'}})});
  assert.equal(await page.evaluate(async()=>{try{await api('/api/action',{miner:'gamma',action:'power',value:42});return false}catch{return true}}),true);
  assert.equal(commands,1,'a failed command must not be replayed after execution may have started');
  assert.deepEqual(failures,[]);console.log('PASS hub: real Python API, secret filtering, offline aggregation, diagnostics, power forwarding, channel, translation, history and mobile layout');
  console.log('PASS hub beta readiness: configured/effective power, preserved edits, late history/fleet responses, connection loss/recovery and safe nonce refresh');
 }finally{if(browser)await browser.close();child.kill('SIGTERM')}
}
main().catch(e=>{console.error(e);process.exitCode=1});
