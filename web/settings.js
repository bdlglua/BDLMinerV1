let S=null,busy=false,restarting=false;const $=id=>document.getElementById(id);
function poolURLChanged(key){
 const input=$('k_'+key),value=input.value.trim();
 if(!value.startsWith('stratum2+tcp://'))return;
 try{
  const url=new URL(value);
  if(!url.hostname||url.username||url.password||url.search||url.hash||(url.port&&Number(url.port)<1))throw Error();
  const suffix=key==='pool2'?'2':'';
  input.value=url.hostname;$('k_proto'+suffix).value='1';
  if(url.port)$('k_port'+suffix).value=url.port;
  if(url.pathname.length>1)$('k_sv2key'+suffix).value=url.pathname.slice(1);
  input.setCustomValidity('');dirtyState();sync();
 }catch(e){input.setCustomValidity(t('Enter a valid value.'))}
}
$('bh').textContent=LANG==='fr'?t('backup_help'):'The backup contains all settings, passwords and lifetime statistics. Keep it private.';
function sync(){
 const off=busy||restarting||!S;
 for(const id of ['save','backup','restore','restart','discard'])$(id).disabled=off;
 const usb=S?.capabilities?.ota===false;
 $('upload').disabled=off||usb||!$('fw').files.length;
 if(usb)$('firmware-help').textContent=t('This board requires a USB firmware update.');
 $('retry').disabled=busy||restarting;
 document.querySelectorAll('#f input,#f select,#f button,#fw,#imp').forEach(i=>i.disabled=busy||restarting);
 if($('k_ua')){$('k_ua').disabled=off||$('k_uaauto')?.value!=='false';$('k_ua').required=$('k_uaauto')?.value==='false';$('k_ua').maxLength=64}
 for(const [protocol,key]of [['proto','sv2key'],['proto2','sv2key2']])if($('k_'+key)){$('k_'+key).disabled=off||$('k_'+protocol)?.value!=='1';$('k_'+key).required=$('k_'+protocol)?.value==='1';$('k_'+key).maxLength=64}
}
// Presentation only: titles, hints, units and choices for the settings the
// miner describes in /api/settings. Unknown keys keep the miner's own label.
const GROUPS={Pool:['◎','Where your miner sends its work and how it introduces itself.'],Display:['▣','The miner\'s screen: brightness, colours, night mode and clock.'],Notifications:['◉','Phone notifications, webhook and alerts.'],Safety:['◇','Temperature, power and automatic restarts.'],Network:['⌁','Fallback WiFi, Home Assistant, other miners, password and updates.']};
const SECTIONS={pool:'Main pool',uaauto:'Identity',pool2:'Backup pool',bright:'Screen',nightfrom:'Night mode',tz:'Clock and price',ntfy:'ntfy',hook:'Webhook',alertpct:'Alerts',maxtemp:'Heat and power',rebooth:'Recovery',wifi2:'Fallback WiFi',mqtt:'Home Assistant (MQTT)',peers:'Web pages',update:'Updates',count:'Anonymous statistics'};
const FIELDS={
 pool:['Pool host','A pasted stratum+tcp:// or stratum2+tcp:// address is split for you.'],port:['Pool port'],
 addr:['Bitcoin address','Your payout address. A worker name can follow: address.worker'],pass:['Pool password','Usually x.'],
 diff:['Suggested difficulty','Share difficulty asked of the pool; it may choose another.'],proto:['Protocol'],
 sv2key:['Stratum V2 authority key','Given by the pool for encrypted V2 connections.'],
 uaauto:['User agent','How the miner names itself to the pool.'],ua:['Manual user agent','Up to 64 characters.'],
 pool2:['Backup pool host','Used when the main pool keeps failing. Empty = none.'],port2:['Backup pool port'],proto2:['Backup protocol'],
 sv2key2:['Backup Stratum V2 authority key'],rotateh:['Alternate pools every','0 = switch only when the main pool fails.','h'],
 bright:['Brightness','','%'],rot:['Rotation'],autorot:['Follow the accelerometer','M5 devices with a motion sensor: the picture turns with the device.'],
 lang:['Language','Screen and web pages.'],theme:['Colour theme','Accent colour of the screen.'],
 cycle:['Next screen every','0 = only with the button.','s'],nightfrom:['Night mode from','Screen off and no beep. A button press lights it for 30 s.'],
 nightto:['Night mode until'],beep:['Beep on a 32-bit share'],tz:['Time zone','POSIX TZ, e.g. CET-1CEST,M3.5.0,M10.5.0/3 or UTC0'],fiat:['Price currency'],
 ntfy:['ntfy topic','A long, hard-to-guess name. Empty = off.'],ntfysrv:['ntfy server'],notify32:['Also every 32-bit share'],
 hook:['Webhook URL','JSON POST for Discord, Telegram… It holds a token: keep it private. Empty = off.'],
 summary:['Daily summary at','Average hashrate, best share, shares and pool connection of the past day. Off = none.'],
 alertpct:['Hashrate alert below','Share of the usual full-power hashrate. 0 = off.','%'],alertmin:['… for at least','','min'],
 maxtemp:['Pause mining above','0 = no thermal limit.','°C'],power:['Mining power','On USB and battery. 0 = paused.','%'],
 rebooth:['Restart after no accepted share for','0 = off.','h'],saver:['Battery saver','On battery: dim the screen and mine at half speed.'],
 wifi2:['Fallback WiFi 1','Network name.'],wpass2:['Fallback WiFi 1 password'],wifi3:['Fallback WiFi 2','Network name.'],wpass3:['Fallback WiFi 2 password'],
 mqtt:['MQTT broker','Your Home Assistant broker. Empty = off.'],mqttport:['MQTT port'],mqttuser:['MQTT user'],mqttpass:['MQTT password'],
 peers:['Other miners','Shown on the dashboard: IP addresses or names, comma separated.'],
 webpass:['Web password','Needed to change settings and install firmware from these pages.'],
 update:['Updates from GitHub','Signed releases of this firmware.'],count:['Anonymous usage count','Once a day, downloads a 2-byte file of this version\'s release. Nothing is sent.'],sharehr:['Share my hashrate anonymously','Every 5 minutes, sends the firmware version, the board and the hashrate, to count the total of all BDLMiner miners live. No address, name or identifier.']};
const hours=[[255,'Off'],...Array.from({length:24},(_,h)=>[h,String(h).padStart(2,'0')+':00'])];
const CHOICES={proto:[[0,'Stratum V1'],[1,'Stratum V2 (Noise)']],proto2:[[0,'Stratum V1'],[1,'Stratum V2 (Noise)']],uaauto:[['true','Automatic'],['false','Manual']],
 asicmhz:Array.from({length:12},(_,n)=>{const mhz=525+n*25;return[mhz,n?mhz+' MHz':'525 MHz (stock)']}),
 lang:[[0,'English'],[1,'Français']],rot:[[0,'Portrait'],[1,'Landscape'],[2,'Portrait, upside down'],[3,'Landscape, upside down']],
 theme:[[0,'Orange'],[1,'Green'],[2,'Blue'],[3,'Purple'],[4,'Pink']],nightfrom:hours,nightto:hours,
 summary:hours,update:[[0,'Off'],[1,'Check and offer'],[2,'Install automatically']],fiat:['USD','EUR','GBP','CAD','CHF','AUD','JPY'].map(c=>[c,c])};
const THEMES=['#f7931a','#38d07a','#3b9cf6','#a66bf5','#ec5aa0'];
function help(text){const p=document.createElement('p');p.className='field-help';p.textContent=text;return p}
function render(data){
 const f=$('f');f.replaceChildren();$('settings-nav').replaceChildren();const groups=new Map();let section=null;
 for(const[k,type,group,label,limits]of data.schema){
  let fs=groups.get(group);
  if(!fs){
   groups.set(group,fs=document.createElement('fieldset'));fs.id=group==='Safety'?'performance':'group-'+group.toLowerCase();
   const [icon,about]=GROUPS[group]||['•',''],lg=document.createElement('legend'),mark=document.createElement('span');
   mark.className='group-icon';mark.setAttribute('aria-hidden','true');mark.textContent=icon;lg.append(mark,t(group));fs.appendChild(lg);
   if(about){const p=document.createElement('p');p.className='group-about';p.textContent=t(about);fs.appendChild(p)}
   f.appendChild(fs);section=null;
   const link=document.createElement('a');link.href='#'+fs.id;link.dataset.group=fs.id;
   const linkIcon=mark.cloneNode(true);link.append(linkIcon,t(group));link.onclick=()=>{$('settings-search').value='';filterSettings()};$('settings-nav').appendChild(link);
  }
  if(SECTIONS[k]||!section||section.parentNode!==fs){
   section=document.createElement('div');section.className='subsection';
   if(SECTIONS[k]){const h=document.createElement('h3');h.textContent=t(SECTIONS[k]);section.appendChild(h)}
   fs.appendChild(section);
  }
  const [title,hint,unit]=FIELDS[k]||[LANG=='fr'&&FR_SET[k]||label],options=CHOICES[k],value=data.values[k];
  const l=document.createElement('label'),i=document.createElement(options?'select':'input');l.textContent=t(title);i.id='k_'+k;l.htmlFor=i.id;
  if(options){
   for(const[v,name]of options){const option=document.createElement('option');option.value=String(v);option.textContent=t(name);i.appendChild(option)}
   const current=String(value??(k==='asicmhz'?600:k==='uaauto'?true:0));
   if(![...i.options].some(o=>o.value===current)){const option=document.createElement('option');option.value=current;option.textContent=current;i.appendChild(option)}
   i.value=current;if(k==='asicmhz')i.required=true;
  }else if(type=='bool'){i.type='checkbox';i.className='switch';i.setAttribute('role','switch');i.checked=value}
  else{
   i.type=type=='secret'?'password':type=='text'?'text':'number';i.value=value??'';
   if(type=='number'||type=='decimal'){i.required=true;i.step=type=='decimal'?'any':'1';i.min=limits?.min??0;if(limits?.max!=null)i.max=limits.max;i.inputMode=type=='decimal'?'decimal':'numeric'}
   if(k==='pool'||k==='addr')i.required=true;
   if(type=='text'||type=='secret'){i.spellcheck=false;i.autocapitalize='off'}
  }
  i.addEventListener('input',()=>{i.setCustomValidity('');dirtyState();if(options)sync()});
  if(options)i.addEventListener('change',()=>{dirtyState();sync()});
  if(k==='pool'||k==='pool2')i.addEventListener('change',()=>poolURLChanged(k));
  const row=document.createElement('div');row.className='field'+(type==='bool'?' boolean':'');row.dataset.key=k;
  const text=document.createElement('div');text.className='field-text';text.appendChild(l);
  if(hint){const p=document.createElement('p');p.className='field-hint';p.id='h_'+k;p.textContent=t(hint);text.appendChild(p);i.setAttribute('aria-describedby',p.id)}
  row.dataset.search=(text.textContent+' '+label+' '+k).toLocaleLowerCase();row.appendChild(text);
  const control=document.createElement('div');control.className='field-control';control.appendChild(i);row.appendChild(control);
  if(unit){const u=document.createElement('span');u.className='unit';u.setAttribute('aria-hidden','true');u.textContent=unit;control.classList.add('with-unit');control.appendChild(u)}
  if(k==='theme'){const swatch=document.createElement('span');swatch.className='swatch';swatch.setAttribute('aria-hidden','true');const paint=()=>swatch.style.background=THEMES[Number(i.value)]||THEMES[0];i.addEventListener('change',paint);paint();control.prepend(swatch)}
  if(type==='secret'){const show=document.createElement('button');show.type='button';show.className='secondary secret-toggle';show.textContent=t('Show');show.setAttribute('aria-label',t('Show')+' '+l.textContent);show.onclick=()=>{const visible=i.type==='password';i.type=visible?'text':'password';show.textContent=t(visible?'Hide':'Show');show.setAttribute('aria-label',t(visible?'Hide':'Show')+' '+l.textContent)};control.appendChild(show)}
  if(k==='pool')row.appendChild(help(t('Choose automatic or manual user agent below. Each pool has its own protocol and authority key.')));
  if(k==='proto'||k==='proto2')row.appendChild(help(t('Stratum V2 needs the pool\'s V2 port and authority key. It does not change share difficulty.')));
  if(k==='asicmhz')row.appendChild(help(t('Profiles above 525 MHz are experimental, up to 800 MHz. Default: 600 MHz. Voltage stays at 1150 mV. Instability triggers a 525 MHz fallback until restart.')));
  if(k==='power'){const h=document.createElement('div');h.className='field-help';h.textContent=t('At 100%, mining runs at full available speed. Temperature protection can still reduce it.');const presets=document.createElement('div');presets.className='presets';for(const [name,v]of [['Full performance',100],['Balanced',75],['Quiet',50]]){const button=document.createElement('button');button.type='button';button.className='secondary';button.id='preset-'+v;button.textContent=t(name)+' · '+v+' %';button.onclick=()=>{i.value=v;i.dispatchEvent(new Event('input',{bubbles:true}))};presets.appendChild(button)}h.appendChild(presets);row.appendChild(h)}
  if(k==='maxtemp'){const h=help('');const update=()=>{const limit=Number(i.value);h.textContent=limit>=30?t('Throttling starts at')+' '+(limit-10)+' °C. '+t('Mining pauses at')+' '+limit+' °C.':limit===0?t('Thermal protection is disabled.'):''};i.addEventListener('input',update);update();row.appendChild(h)}
  if(k==='hook'){const h=help(t('Discord: channel settings › Integrations › Webhooks › Copy URL. Telegram: https://api.telegram.org/bot<token>/sendMessage?chat_id=<chat>. Save and restart first, then:'));const test=document.createElement('button');test.type='button';test.className='secondary';test.id='notify-test';test.textContent=t('Send a test notification');test.onclick=testNotification;const out=document.createElement('span');out.id='notify-result';out.setAttribute('role','status');out.setAttribute('aria-live','polite');const line=document.createElement('div');line.className='presets';line.append(test,out);h.appendChild(line);row.appendChild(h)}
  section.appendChild(row);
 }
 S=data;$('msg').textContent='';filterSettings();dirtyState();watchSections();
}
async function load(){
 if(busy||restarting)return;
 busy=true;S=null;sync();$('msg').textContent=t('Loading settings…');$('retry').hidden=true;
 try{
  const data=await request('/api/settings');
  if(!data||!Array.isArray(data.schema)||!data.values)throw Error(t('Invalid response from the miner.'));
  render(data);
 }catch(e){$('msg').textContent=errorText(e);$('retry').hidden=false}
 finally{busy=false;sync()}
}
function discardChanges(){if(S&&!busy&&!restarting){render(S);sync()}}
// Highlights the group being read in the side navigation.
let sectionObserver=null;
function watchSections(){
 if(!('IntersectionObserver'in window))return;
 if(sectionObserver)sectionObserver.disconnect();
 sectionObserver=new IntersectionObserver(entries=>{for(const e of entries)if(e.isIntersecting)document.querySelectorAll('#settings-nav a').forEach(a=>a.classList.toggle('active',a.dataset.group===e.target.id))},{rootMargin:'-20% 0px -70% 0px'});
 document.querySelectorAll('#f fieldset').forEach(fs=>sectionObserver.observe(fs));
}
function values(){
 const v={};
 for(const[k,type]of S.schema){
  const i=$('k_'+k);v[k]=fieldValue(k,type);
  if((k==='pool'||k==='addr')&&!v[k].trim())i.setCustomValidity(t('Enter a valid value.'));
  if(type==='decimal'&&!(v[k]>0))i.setCustomValidity(t('The difficulty must be greater than zero.'));
  if(k==='maxtemp'&&v[k]>0&&v[k]<30)i.setCustomValidity(t('The temperature must be 0 or at least 30 °C.'));
  if((k==='nightfrom'||k==='nightto')&&v[k]>23&&v[k]!==255)i.setCustomValidity(t('The hour must be 0–23 or 255 to disable night mode.'));
 }
 return v;
}
async function saveSettings(event){
 if(event)event.preventDefault();if(busy||restarting||!S)return;
 const v=values();
 if(!$('f').checkValidity()){$('settings-search').value='';filterSettings();$('f').reportValidity();return}
 busy=true;sync();
 try{
  const answer=await request('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(v)},'text');
  let result=null;try{result=JSON.parse(answer)}catch(e){}
  if(result&&result.restart===false){
   // Applied by the miner at once: the page stays usable.
   const language=v.lang!==undefined&&v.lang!==S.values.lang;S.values=v;dirtyState();
   $('msg').textContent=t(result.changed?'Saved and applied.':'Nothing to save.');
   if(language)setTimeout(()=>location.reload(),600);
  }else{restarting=true;$('msg').textContent=t('Saved, restarting… reload in 15 s')}
 }
 catch(e){$('msg').textContent=errorText(e)}finally{busy=false;sync()}
}
async function imp(input){
 const file=input.files[0];if(!file||busy||restarting)return;
 busy=true;sync();
 try{
  const raw=await file.text();let parsed;
  try{parsed=JSON.parse(raw)}catch(e){throw Error(t('Not a backup file'))}
  if(!parsed||typeof parsed!=='object'||Array.isArray(parsed))throw Error(t('Not a backup file'));
  if(!confirm(t('Restore this backup? The miner restarts afterwards.')))return;
  // Send original text: parsing/stringifying would round uint64 hash totals.
  await request('/api/backup',{method:'POST',headers:{'Content-Type':'application/json'},body:raw},'text');
  restarting=true;$('msg').textContent=t('Backup restored. The miner is restarting…');
 }catch(e){$('msg').textContent=errorText(e)}finally{input.value='';busy=false;sync()}
}
async function uploadFirmware(){
 if(busy||restarting||!S)return;
 const file=$('fw').files[0];if(!file){$('up').textContent=t('Select a firmware file first.');return}
 busy=true;sync();
 try{
  if(!/\.bin$/i.test(file.name)||/factory(?:\.bin)?$/i.test(file.name))throw Error(t('Use a firmware .bin file, not a factory image.'));
  if(!file.size||new Uint8Array(await file.slice(0,1).arrayBuffer())[0]!==0xE9)throw Error(t('Invalid firmware file.'));
  const x=new XMLHttpRequest(),data=new FormData();data.append('firmware',file);
  x.upload.onprogress=e=>{if(e.lengthComputable)$('up').textContent=e.loaded===e.total?t('Upload complete. Validating firmware…'):Math.round(e.loaded*100/e.total)+' %'};
  const finish=message=>{$('up').textContent=message;busy=false;sync()};
  x.onload=()=>{
   if(x.status>=200&&x.status<300){restarting=true;finish(t('Firmware updated. The miner is restarting…'))}
   else finish(x.status===401?t('Authentication required. Reload this page and sign in.'):t(x.responseText)||t('Upload failed'));
  };
  x.onerror=()=>finish(t('Upload failed'));x.ontimeout=()=>finish(t('Upload timed out. Please try again.'));x.onabort=()=>finish(t('Upload cancelled.'));
  x.open('POST','/update');x.timeout=120000;x.send(data);
 }catch(e){$('up').textContent=errorText(e);busy=false;sync()}
}
load();

function fieldValue(k,type){const input=$('k_'+k);return type==='bool'?(input.tagName==='SELECT'?input.value==='true':input.checked):type==='number'||type==='decimal'?Number(input.value):input.value}
function dirtyState(){
 if(!S)return;
 let count=0,restart=false;const live=S.live||[];
 for(const[k,type]of S.schema){const changed=fieldValue(k,type)!==S.values[k];$('k_'+k).closest('.field').classList.toggle('changed',changed);if(changed){count++;if(!live.includes(k))restart=true}}
 $('dirty').textContent=count?count+' '+t(count>1?'unsaved changes':'unsaved change')+' · '+t(restart?'needs a restart':'applied right away'):t(live.length?'Display, alerts and power apply right away; other changes restart the miner.':'Changes take effect after restart.');
 $('save').textContent=t(count&&!restart?'Save':'Save and restart');
 document.querySelector('.save-bar').dataset.dirty=count>0;$('discard').hidden=!count;
}
function filterSettings(){
 const query=$('settings-search').value.trim().toLocaleLowerCase();let visible=0;
 document.querySelectorAll('#f fieldset').forEach(fs=>{let found=0;fs.querySelectorAll('.subsection').forEach(sub=>{let inside=0;sub.querySelectorAll('.field').forEach(row=>{row.hidden=!row.dataset.search.includes(query);if(!row.hidden)inside++});sub.hidden=!inside;found+=inside});fs.hidden=!found;visible+=found});
 $('settings-empty').hidden=!!visible||!S;
}
$('settings-search').addEventListener('input',filterSettings);
// The miner sends in the background (with retries): wait for its result.
async function testNotification(){
 const out=$('notify-result'),button=$('notify-test');if(busy||restarting||button.disabled)return;
 button.disabled=true;out.textContent=t('Sending…');
 try{
  const before=(await request('/api/notify-test')).id;
  await request('/api/notify-test',{method:'POST'},'text');
  for(let n=0;n<50;n++){
   await new Promise(r=>setTimeout(r,2000));
   const r=await request('/api/notify-test');
   if(r.id!==before){out.textContent='ntfy: '+t(r.ntfy)+' · webhook: '+t(r.webhook);return}
  }
  out.textContent=t('No result yet: see the log page.');
 }catch(e){out.textContent=errorText(e)}
 finally{button.disabled=busy||restarting}
}
async function restartMiner(){
 if(busy||restarting||!S||!confirm(t('Restart this miner?')))return;
 busy=true;sync();
 try{await request('/api/restart',{method:'POST'},'text');restarting=true;$('msg').textContent=t('Restarting…')}
 catch(e){$('msg').textContent=errorText(e)}finally{busy=false;sync()}
}
