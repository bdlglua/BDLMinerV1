let S=null,busy=false,restarting=false;const $=id=>document.getElementById(id);
$('bh').textContent=LANG==='fr'?t('backup_help'):'The backup contains all settings, passwords and lifetime statistics. Keep it private.';
function sync(){
 const off=busy||restarting||!S;
 for(const id of ['save','backup','restore','restart'])$(id).disabled=off;
 const usb=S?.capabilities?.ota===false;
 $('upload').disabled=off||usb||!$('fw').files.length;
 if(usb)$('firmware-help').textContent=t('This board requires a USB firmware update.');
 $('retry').disabled=busy||restarting;
 document.querySelectorAll('#f input,#f select,#f button,#fw,#imp').forEach(i=>i.disabled=busy||restarting);
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
   const l=document.createElement('label'),i=document.createElement('input');l.textContent=LANG=='fr'&&FR_SET[k]||label;i.id='k_'+k;l.htmlFor=i.id;
   if(type=='bool'){i.type='checkbox';i.checked=data.values[k]}
   else{
    i.type=type=='secret'?'password':type=='text'?'text':'number';i.value=data.values[k]??'';
    if(type=='number'||type=='decimal'){
     i.required=true;i.step=type=='decimal'?'any':'1';i.min=limits?.min??0;
     if(limits?.max!=null)i.max=limits.max;
    }
    if(k==='pool'||k==='addr')i.required=true;
   }
   i.addEventListener('input',()=>{i.setCustomValidity('');dirtyState()});
   const row=document.createElement('div');row.className='field'+(type==='bool'?' boolean':'');row.dataset.search=(l.textContent+' '+k).toLocaleLowerCase();row.appendChild(l);
   const control=document.createElement('div');control.className='field-control';control.appendChild(i);row.appendChild(control);
   if(type==='secret'){const show=document.createElement('button');show.type='button';show.className='secondary secret-toggle';show.textContent=t('Show');show.setAttribute('aria-label',t('Show')+' '+l.textContent);show.onclick=()=>{const visible=i.type==='password';i.type=visible?'text':'password';show.textContent=t(visible?'Hide':'Show');show.setAttribute('aria-label',t(visible?'Hide':'Show')+' '+l.textContent)};control.appendChild(show)}
   if(k==='pool'){const help=document.createElement('p');help.className='field-help';help.textContent=t('The Stratum user agent is selected automatically for each pool.');row.appendChild(help)}
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
  const i=$('k_'+k);v[k]=type=='bool'?i.checked:type=='number'||type=='decimal'?Number(i.value):i.value;
  if((k==='pool'||k==='addr')&&!v[k].trim())i.setCustomValidity(t('Enter a valid value.'));
  if(type==='decimal'&&!(v[k]>0))i.setCustomValidity(t('The difficulty must be greater than zero.'));
  if(k==='maxtemp'&&v[k]>0&&v[k]<30)i.setCustomValidity(t('The temperature must be 0 or at least 30 °C.'));
  if((k==='nightfrom'||k==='nightto')&&v[k]>23&&v[k]!==255)i.setCustomValidity(t('The hour must be 0–23 or 255 to disable night mode.'));
 }
 return v;
}
async function saveSettings(event){
 if(event)event.preventDefault();if(busy||restarting||!S)return;
 const v=values();if(!$('f').reportValidity())return;
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
 for(const[k,type]of S.schema){const input=$('k_'+k);const value=type==='bool'?input.checked:type==='number'||type==='decimal'?Number(input.value):input.value;if(value!==S.values[k])count++}
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
