// Exercise packaged Pages files, their real interfaces and isolated demo requests.
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const {spawn}=require('node:child_process');
const {chromium}=require('playwright');
async function main(){
 const child=spawn('python3',[path.join(__dirname,'site_fixture.py')],{stdio:['ignore','pipe','pipe']});let stderr='';child.stderr.on('data',d=>stderr+=d);
 const url=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('Site fixture did not start: '+stderr)),15000);child.stdout.once('data',d=>{clearTimeout(timer);resolve(d.toString().trim())});child.once('exit',code=>{clearTimeout(timer);reject(Error('Site fixture exited '+code+': '+stderr))})});
 let browser;
 try{
  browser=await chromium.launch({headless:true,executablePath:process.env.BDL_CHROMIUM_PATH||(fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined),args:['--no-sandbox']});
  const context=await browser.newContext({viewport:{width:1440,height:1000}}),page=await context.newPage(),errors=[],requests=[];
  page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>requests.push(r.url()));
  if(process.env.BDL_VERBOSE)page.on('console',message=>{if(message.type()==='error')console.error('BROWSER:',message.text())});
  await page.route('https://**/*',route=>route.fulfill({status:200,contentType:'text/javascript',body:''}));
  await page.addInitScript(()=>{window.WebSocket=class{constructor(){this.readyState=1;setTimeout(()=>this.onopen?.(),0)}send(){}close(){}}});
  await page.goto(url);await page.locator('[data-t="ctaInstall"]').waitFor();
  assert.equal(await page.locator('[data-t="ctaInstall"]').textContent(),'Flash');
  assert.equal(await page.locator('#board-comparison,#demo-frame,#faq-search,#support').count(),0,'home must contain only home content');
  assert.equal(await page.locator('main h1').count(),1);
  await page.locator('#site-links a[href="boards.html"]').click();await page.waitForURL(url+'/boards.html');
  await page.waitForSelector('.board-card');
  assert.equal(await page.locator('#board-comparison .board-card').count(),2);
  assert.match(await page.locator('#board-comparison').textContent(),/1\.00 MH\/s/);
  assert.match(await page.locator('#board-comparison').textContent(),/1\.07 TH\/s/);
  await page.locator('[data-filter="s3"]').click();assert.doesNotMatch(await page.locator('.comparison-grid').textContent(),/1\.00 MH\/s/);
  assert.match(await page.locator('.comparison-grid').textContent(),/To be measured/);
  await page.locator('[data-board-env="lilygo-t-display-s3"]').click();await page.waitForURL(url+'/flash.html?board=lilygo-t-display-s3');
  assert.equal(await page.locator('#board').inputValue(),'lilygo-t-display-s3');
  assert.equal(await page.locator('#installer').getAttribute('manifest'),'manifest-lilygo-t-display-s3.json');
  await page.goto(url+'/flash.html?board=../../unknown');
  assert.equal(await page.locator('#board').inputValue(),'lilygo-t-display-s3','invalid profiles must not replace the saved board');
  await page.goto(url+'/start.html');await page.locator('#route-axeos').click();
  assert.match(await page.locator('#route-content').textContent(),/601.*5 V/s);
  await page.locator('#route-content a[href="flash.html?board=bitaxe-gamma-601"]').click();await page.waitForURL(url+'/flash.html?board=bitaxe-gamma-601');
  assert.equal(await page.locator('#board').inputValue(),'bitaxe-gamma-601');
  await page.goto(url+'/faq.html');await page.locator('#faq-search').fill('USB');
  assert.ok(await page.locator('#faq-list details').count()>0);
  await page.locator('#faq-list summary').first().click();assert.equal(await page.locator('#faq-list details').first().getAttribute('open'),'');
  await page.locator('#faq-search').fill('zzzz-no-match');assert.equal(await page.locator('#faq-empty').isVisible(),true);
  await page.goto(url+'/gallery.html');await page.locator('.gallery-invite').waitFor();
  assert.equal(await page.locator('#gallery .gallery-card').count(),0);
  assert.equal(await page.locator('#gallery a[href*="community_setup.yml"]').count(),1);
  await page.locator('#nav-support').click();await page.waitForURL(url+'/support.html');
  assert.equal(await page.locator('#support a[href="https://github.com/sponsors/bdlglua"]').count(),1);
  await page.locator('#lang').click();assert.equal(await page.locator('#nav-support').textContent(),'Soutenir');
  assert.match(await page.locator('#support').textContent(),/Cartes de test/);
  await page.locator('#site-links a[href="flash.html"]').click();await page.waitForURL(url+'/flash.html');
  assert.equal(await page.locator('#installer [data-t="install"]').textContent(),'Flasher');
  await page.goto(url);assert.equal(await page.locator('[data-t="ctaInstall"]').textContent(),'Flasher');
  await page.goto(url+'/start.html');await page.locator('#route-axeos').click();
  assert.match(await page.locator('#discovery-start').textContent(),/Alimentation régulée 5 V/);
  await page.locator('#site-links a[href="demo.html"]').click();await page.waitForURL(url+'/demo.html');
  const frame=page.frameLocator('#demo-frame');await frame.locator('#m .hero').waitFor();
  assert.match(await frame.locator('.demo-notice').textContent(),/Données fictives/);
  await frame.locator('a[href="settings.html"]').first().click();await frame.locator('#k_power').waitFor();
  await frame.locator('#k_power').fill('50');await frame.locator('#save').click();
  await frame.locator('#msg').filter({hasText:/appliqu|applied/i}).waitFor({timeout:5000}).catch(async error=>{console.error('Demo save status:',await frame.locator('#msg').textContent(),errors,await frame.locator('input:invalid,select:invalid').evaluateAll(nodes=>nodes.map(n=>({id:n.id,value:n.value,message:n.validationMessage}))),await frame.locator('#f').evaluate(f=>({onsubmit:f.getAttribute('onsubmit'),handler:typeof saveSettings,form:document.getElementById('save').form?.id,busy:window.busy})));throw error});
  await frame.locator('a[href="index.html"]').first().click();await frame.locator('#m .hero').filter({hasText:/502/}).waitFor();
  await frame.locator('.demo-notice select').selectOption('pool');
  await frame.locator('#mining-state').filter({hasText:/attente du pool/}).waitFor();
  const demoFrame=page.frames().find(f=>f.url().includes('/demo/miner/'));
  assert.equal(await demoFrame.evaluate(async()=>{try{await fetch('http://192.168.1.1/api/settings',{method:'POST',body:'{}'});return false}catch{return true}}),true);
  await page.locator('#demo-tab-hub').click();const hub=page.frameLocator('#demo-frame');await hub.locator('.miner').first().waitFor();
  assert.equal(await hub.locator('.miner').count(),3);assert.equal(await hub.locator('.miner.offline').count(),1);
  const gamma=hub.locator('[data-id="gamma"]');await gamma.locator('input').fill('50');await gamma.getByRole('button',{name:'Appliquer',exact:true}).click();
  await gamma.locator('.result').filter({hasText:/Commande envoyée/}).waitFor();
  assert.match(await gamma.locator('.rate').textContent(),/535 G/);
  await gamma.getByRole('button',{name:'Diagnostiquer le pool'}).click();await gamma.locator('.result').filter({hasText:/Stratum V2/}).waitFor();
  const gammaPage=await context.newPage();gammaPage.on('pageerror',e=>errors.push(e.message));gammaPage.on('request',r=>requests.push(r.url()));
  await gammaPage.goto(await gamma.locator('a').first().getAttribute('href'));
  await gammaPage.locator('#m .hero').filter({hasText:/TH\/s/}).waitFor();
  assert.match(await gammaPage.locator('#device-grid').textContent(),/BM1370|525 MHz/);
  await gammaPage.close();
  assert.equal(requests.some(u=>new URL(u).origin===url&&new URL(u).pathname.startsWith('/api/')),false,'demo calls must stay in memory');
  assert.equal(requests.some(u=>/^http:\/\/192\./.test(u)),false,'the demo must never contact a LAN miner');
  const pages={home:'index.html',start:'start.html',boards:'boards.html',flash:'flash.html',demo:'demo.html',faq:'faq.html',releases:'releases.html',stats:'stats.html',gallery:'gallery.html',support:'support.html'};
  for(const prefix of ['', '/beta', '/BDLMinerV2', '/BDLMinerV2/beta']){
   const base=url+prefix;
   for(const [key,name]of Object.entries(pages)){
    const response=await page.request.get(base+'/'+name);assert.equal(response.status(),200,name+' must be a real page');
    const html=await response.text();assert.ok(html.includes('data-page="'+key+'"'));assert.ok(!html.includes('__PAGE_CONTENT__'));
    assert.equal(html.includes('src="https://unpkg.com/esp-web-tools@'),key==='flash','load the USB tool only on its own page');
   }
   await page.goto(base+'/#support');await page.waitForURL(base+'/support.html');await page.locator('.support-panel').waitFor();
   assert.equal(await page.locator('main h1').count(),1);
   await page.goto(base+'/releases.html');await page.locator('.release-card').first().waitFor();
   assert.equal(await page.locator('#discovery-news a[href="'+(prefix.endsWith('/beta')?'../':'./')+'flash.html"]').count(),1);
   assert.equal(await page.locator('#discovery-news a[href="'+(prefix.endsWith('/beta')?'./':'beta/')+'flash.html"]').count(),1);
   await page.goto(base+'/boards.html');await page.locator('[data-filter="s3"]').click();
   await page.locator('[data-board-env="lilygo-t-display-s3"]').click();await page.waitForURL(base+'/flash.html?board=lilygo-t-display-s3');
   assert.equal(await page.locator('#board').inputValue(),'lilygo-t-display-s3');
   await page.goto(base+'/#demo');await page.waitForURL(base+'/demo.html');await page.frameLocator('#demo-frame').locator('#m .hero').waitFor();
   assert.ok(page.frames().some(frame=>frame.url().startsWith(base+'/demo/miner/')));
  }
  for(const name of Object.values(pages)){
   await page.setViewportSize({width:375,height:812});await page.goto(url+'/'+name);
   await page.locator('main h1').first().waitFor();
   assert.equal(await page.locator('main h1').count(),1,name+' should have one main heading');
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,name+' should fit mobile');
  }
  await page.locator('#menu').click();await page.locator('#nav-more summary').click();
  await page.locator('#nav-more a[href="faq.html"]').click();await page.waitForURL(url+'/faq.html');
  assert.equal(await page.locator('#site-links').evaluate(e=>e.classList.contains('open')),false);
  await page.goto(url+'/beta/flash.html?board=esp32c3-headless');
  assert.equal(await page.locator('#board').inputValue(),'esp32c3-headless');
  const manifest=await (await page.request.get(url+'/beta/'+await page.locator('#installer').getAttribute('manifest'))).json();
  assert.equal(manifest.version,'2.0.2-beta.1');
  assert.deepEqual(errors,[],'uncaught site/demo browser errors');
  if(process.env.BDL_SCREENSHOT_DIR){fs.mkdirSync(process.env.BDL_SCREENSHOT_DIR,{recursive:true});await page.setViewportSize({width:1440,height:1000});await page.goto(url);await page.locator('[data-t=ctaInstall]').waitFor();await page.screenshot({path:path.join(process.env.BDL_SCREENSHOT_DIR,'site-discovery.png'),fullPage:true})}
  console.log('PASS site: separate pages, Flasher label, shared navigation, board transfer, old-link redirects, both hosting prefixes/channels, mobile layouts and isolated interactive demos');
 }finally{if(browser)await browser.close();child.kill('SIGTERM')}
}
main().catch(error=>{console.error(error);process.exitCode=1});
