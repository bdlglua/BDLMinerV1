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
function publicPoolPreset(protocolKey,version){
 const backup=protocolKey==='proto2',fields=[[backup?'pool2':'pool','public-pool.io'],[backup?'port2':'port',version===1?23330:3333],[protocolKey,version]];
 if(version===1)fields.push([backup?'sv2key2':'sv2key','9c4zpyJ2ndm4e8sP2uNc1VNCGxYjqaxWS6wUCjk8zFj6njFquH6']);
 for(const[key,value]of fields){const input=$('k_'+key);if(input){input.value=value;input.dispatchEvent(new Event('input',{bubbles:true}))}}
 sync();
}
$('bh').textContent=LANG==='fr'?t('backup_help'):'The backup contains all settings, passwords and lifetime statistics. Keep it private.';
function sync(){
 const off=busy||restarting||!S;
 for(const id of ['save','backup','restore','restart'])$(id).disabled=off;
 const usb=S?.capabilities?.ota===false;
 $('upload').disabled=off||usb||!$('fw').files.length;
 if(usb)$('firmware-help').textContent=t('This board requires a USB firmware update.');
 $('retry').disabled=busy||restarting;
 document.querySelectorAll('#f input,#f select,#f button,#fw,#imp').forEach(i=>i.disabled=busy||restarting);
 if($('k_ua')){$('k_ua').disabled=off||$('k_uaauto')?.value!=='false';$('k_ua').required=$('k_uaauto')?.value==='false';$('k_ua').maxLength=64}
 for(const [protocol,key]of [['proto','sv2key'],['proto2','sv2key2']])if($('k_'+key)){$('k_'+key).disabled=off||$('k_'+protocol)?.value!=='1';$('k_'+key).required=$('k_'+protocol)?.value==='1';$('k_'+key).maxLength=64}
}
async function load(){
 if(busy||restarting)return;
 busy=true;S=null;sync();$('msg').textContent=t('Loading settings…');$('retry').hidden=true;
 try{
  const data=await request('/api/settings');
  if(!data||!Array.isArray(data.schema)||!data.values)throw Error(t('Invalid response from the miner.'));
  const f=$('f');f.replaceChildren();$('settings-nav').replaceChildren();const groups=new Map();
  for(const[k,type,group,label,limits]of data.schema){
   let fs=groups.get(group);if(!fs){groups.set(group,fs=document.createElement('fieldset'));const lg=document.createElement('legend');lg.textContent=t(group);fs.id=group==='Safety'?'performance':'group-'+group.toLowerCase();fs.appendChild(lg);f.appendChild(fs);const link=document.createElement('a');link.href='#'+fs.id;link.textContent=t(group);link.onclick=()=>{$('settings-search').value='';filterSettings()};$('settings-nav').appendChild(link)}
   const l=document.createElement('label'),choice=k==='proto'||k==='proto2'||k==='uaauto'||k==='asicmhz',i=document.createElement(choice?'select':'input');l.textContent=LANG=='fr'&&FR_SET[k]||label;i.id='k_'+k;l.htmlFor=i.id;
   if(choice){for(const[value,name]of k==='asicmhz'?[[525,'525 MHz (stock)'],[550,'550 MHz'],[575,'575 MHz'],[600,'600 MHz']]:k==='uaauto'?[['true','Automatic'],['false','Manual']]:[['0','Stratum V1'],['1','Stratum V2 (Noise)']]){const option=document.createElement('option');option.value=value;option.textContent=t(name);i.appendChild(option)}i.value=String(data.values[k]??(k==='asicmhz'?600:k==='uaauto'?true:0));if(k==='asicmhz')i.required=true}
   else if(type=='bool'){i.type='checkbox';i.checked=data.values[k]}
   else{
    i.type=type=='secret'?'password':type=='text'?'text':'number';i.value=data.values[k]??'';
    if(type=='number'||type=='decimal'){
     i.required=true;i.step=type=='decimal'?'any':'1';i.min=limits?.min??0;
     if(limits?.max!=null)i.max=limits.max;
    }
    if(k==='pool'||k==='addr')i.required=true;
   }
   i.addEventListener('input',()=>{i.setCustomValidity('');dirtyState();if(choice)sync()});
   if(k==='pool'||k==='pool2')i.addEventListener('change',()=>poolURLChanged(k));
   const row=document.createElement('div');row.className='field'+(type==='bool'?' boolean':'');row.dataset.search=(l.textContent+' '+k).toLocaleLowerCase();row.appendChild(l);
   const control=document.createElement('div');control.className='field-control';control.appendChild(i);row.appendChild(control);
   if(type==='secret'){const show=document.createElement('button');show.type='button';show.className='secondary secret-toggle';show.textContent=t('Show');show.setAttribute('aria-label',t('Show')+' '+l.textContent);show.onclick=()=>{const visible=i.type==='password';i.type=visible?'text':'password';show.textContent=t(visible?'Hide':'Show');show.setAttribute('aria-label',t(visible?'Hide':'Show')+' '+l.textContent)};control.appendChild(show)}
   if(k==='pool'){const help=document.createElement('p');help.className='field-help';help.textContent=t('Choose automatic or manual user agent below. Each pool has its own protocol and authority key.');row.appendChild(help)}
   if(k==='proto'||k==='proto2'){
    const help=document.createElement('p');help.className='field-help';help.textContent=t('Stratum V2 needs the pool\'s V2 port and authority key. It does not change share difficulty.');
    const preset=document.createElement('button');preset.type='button';preset.className='secondary';preset.textContent=t('Use Public Pool V2 (solo)');preset.id='public-pool-'+k;
    preset.onclick=()=>publicPoolPreset(k,1);
    const v1=document.createElement('button');v1.type='button';v1.className='secondary';v1.textContent=t('Use Public Pool V1 (solo)');v1.id='public-pool-v1-'+k;v1.onclick=()=>publicPoolPreset(k,0);
    help.appendChild(document.createElement('br'));help.append(v1,' ',preset);const source=document.createElement('a');source.href='https://web.public-pool.io/';source.target='_blank';source.rel='noopener noreferrer';source.textContent=t('Pool connection details');help.append(' · ',source);row.appendChild(help);
   }
   if(k==='asicmhz'){const help=document.createElement('p');help.className='field-help';help.textContent=t('600 MHz is experimental. Voltage stays at 1150 mV. Instability triggers a 525 MHz fallback until restart.');row.appendChild(help)}
   if(k==='power'){const help=document.createElement('div');help.className='field-help';help.textContent=t('At 100%, mining runs at full available speed. Temperature protection can still reduce it.');const presets=document.createElement('div');presets.className='presets';for(const [name,value]of [['Full performance',100],['Balanced',75],['Quiet',50]]){const button=document.createElement('button');button.type='button';button.className='secondary';button.id='preset-'+value;button.textContent=t(name)+' · '+value+' %';button.onclick=()=>{i.value=value;i.dispatchEvent(new Event('input',{bubbles:true}))};presets.appendChild(button)}help.appendChild(presets);row.appendChild(help)}
   if(k==='maxtemp'){const help=document.createElement('p');help.className='field-help';const update=()=>{const limit=Number(i.value);help.textContent=limit>=30?t('Throttling starts at')+' '+(limit-10)+' °C. '+t('Mining pauses at')+' '+limit+' °C.':limit===0?t('Thermal protection is disabled.'):''};i.addEventListener('input',update);update();row.appendChild(help)}
   fs.appendChild(row);
  }
  S=data;$('msg').textContent='';filterSettings();dirtyState();
 }catch(e){$('msg').textContent=errorText(e);$('retry').hidden=false}
 finally{busy=false;sync()}
}
function values(){
 const v={};
 for(const[k,type]of S.schema){
  const i=$('k_'+k);v[k]=type=='bool'?(i.tagName==='SELECT'?i.value==='true':i.checked):type=='number'||type=='decimal'?Number(i.value):i.value;
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
 try{await request('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(v)},'text');restarting=true;$('msg').textContent=t('Saved, restarting… reload in 15 s')}
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

function dirtyState(){
 if(!S)return;
 let count=0;
 for(const[k,type]of S.schema){const input=$('k_'+k);const value=type==='bool'?(input.tagName==='SELECT'?input.value==='true':input.checked):type==='number'||type==='decimal'?Number(input.value):input.value;if(value!==S.values[k])count++}
 $('dirty').textContent=count?count+' '+t('unsaved changes'):t('Changes take effect after restart.');
}
function filterSettings(){
 const query=$('settings-search').value.trim().toLocaleLowerCase();let visible=0;
 document.querySelectorAll('#f fieldset').forEach(fs=>{let found=0;fs.querySelectorAll('.field').forEach(row=>{row.hidden=!row.dataset.search.includes(query);if(!row.hidden)found++});fs.hidden=!found;visible+=found});
 $('settings-empty').hidden=!!visible||!S;
}
$('settings-search').addEventListener('input',filterSettings);
async function restartMiner(){
 if(busy||restarting||!S||!confirm(t('Restart this miner?')))return;
 busy=true;sync();
 try{await request('/api/restart',{method:'POST'},'text');restarting=true;$('msg').textContent=t('Restarting…')}
 catch(e){$('msg').textContent=errorText(e)}finally{busy=false;sync()}
}
