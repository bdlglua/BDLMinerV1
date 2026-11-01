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
  assert.deepEqual(failures,[]);console.log('PASS hub: real Python API, secret filtering, offline aggregation, diagnostics, power forwarding, channel, translation, history and mobile layout');
 }finally{if(browser)await browser.close();child.kill('SIGTERM')}
}
main().catch(e=>{console.error(e);process.exitCode=1});
