#include "web.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "board.h"
#include "core/address.h"
#include "ghupdate.h"
#include "log.h"
#include "market.h"
#include "miner.h"
#include "net.h"
#include "ota.h"
#include "settings.h"
#include "settings_apply.h"
#include "watch.h"
#include "web_validation.h"

namespace {

WebServer server(80);
String hostName;
std::atomic<uint32_t> restartAt{0};  // restart due at this millis(), 0 = none
std::atomic<const char *> restartWhy{""};

// Shared by the pages.
const char kStyle[] PROGMEM = R"CSS(<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#0b0d12;--panel:#161b26;--border:#2b3344;--label:#8c96aa;--fg:#eceef2;--accent:#f7931a;--ok:#38d07a;--bad:#e5484d}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif;padding:16px}
header{display:flex;align-items:center;gap:10px;max-width:960px;margin:0 auto 16px;flex-wrap:wrap}
.coin{width:30px;height:30px;border-radius:50%;background:var(--accent);color:#fff;font-weight:900;display:grid;place-items:center;transform:rotate(14deg)}
h1{font-size:20px;margin:0}h1 b{color:var(--accent)}h2{font-size:14px;color:var(--label);text-transform:uppercase;letter-spacing:.06em;max-width:960px;margin:18px auto 8px}
nav{margin-left:auto;display:flex;gap:14px;align-items:center;font-size:13px}a{color:var(--accent)}
main,.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:10px;max-width:960px;margin:0 auto}
.t{background:var(--panel);border:1px solid var(--border);border-radius:8px;padding:10px 12px;min-width:0}
.t.big{grid-column:span 2;border-top:2px solid var(--accent)}
.l{color:var(--label);font-size:11px;font-weight:700;letter-spacing:.06em;text-transform:uppercase}
.v{font-size:20px;font-weight:700;overflow-wrap:anywhere;font-variant-numeric:tabular-nums}
.big .v{font-size:38px;color:var(--accent)}.s{color:var(--label);font-size:12px;white-space:pre-line}
footer{max-width:960px;margin:16px auto 0;color:var(--label);font-size:12px}
form,.box{max-width:960px;margin:0 auto}label{display:block;margin:8px 0 2px;color:var(--label);font-size:13px}
input{width:100%;padding:7px 9px;border-radius:6px;border:1px solid var(--border);background:var(--panel);color:var(--fg);font:inherit}
input[type=checkbox]{width:auto}button{margin:12px 8px 0 0;padding:8px 14px;border:0;border-radius:6px;background:var(--accent);color:#000;font-weight:700;cursor:pointer}
fieldset{min-inline-size:0;border:1px solid var(--border);border-radius:8px;margin:12px 0;padding:4px 12px 12px}legend{color:var(--accent);font-weight:700}
pre{max-width:960px;margin:0 auto;white-space:pre-wrap;font-size:12px;background:var(--panel);border:1px solid var(--border);border-radius:8px;padding:10px}
@media(max-width:460px){.t.big{grid-column:span 1}}
button:disabled{opacity:.5;cursor:wait}input:invalid{border-color:var(--bad)}
#msg,#up,#log-status{overflow-wrap:anywhere}
</style>)CSS";

// Translations for the pages: t('English') gives the French text when the
// miner's language is French (LANG is set by sendPage()).
const char kI18n[] PROGMEM = R"JS(<script>
const FR={'Dashboard':'Tableau de bord','Settings':'Réglages','Log':'Journal','Hashrate':'Hashrate','Shares':'Shares',
'rejected':'rejetées','pool diff':'diff pool','Best difficulty':'Meilleure difficulté','best ever':'record','32-bit shares':'Shares 32 bits',
'1 every':'1 toutes les','on average':'en moyenne','Block chance':'Chance de bloc','day':'jour','year':'an','Blocks found':'Blocs trouvés',
'lifetime':'depuis le début','Total hashes':'Hashes totaux','uptime':'en marche depuis','Block height':'Hauteur de bloc','net diff':'diff réseau',
'Lifetime':'Depuis le début','hashes':'hashes','shares':'shares','running':'en marche','Hashrate history':'Historique du hashrate',
'5-min averages, last':'moyennes sur 5 min, depuis','avg':'moy.','range':'de','first point after':'premier point après','min of mining':'min de minage',
'Seen by the pool':'Vu par la pool','best':'meilleure','worker(s)':'worker(s)','min ago':'min','no stats API known for this pool':'pas d\'API de statistiques connue pour cette pool',
'Best shares':'Meilleures shares','BTC price':'Prix du BTC','sats':'sats','Fees sat/vB':'Frais sat/vB','fast / 30 min / 1 h':'rapide / 30 min / 1 h',
'Network hashrate':'Hashrate du réseau','tx in mempool':'tx dans la mempool','Next retarget':'Prochain ajustement','in':'dans','blocks':'blocs','done':'fait',
'Pool (backup)':'Pool (secours)','Pool':'Pool','Payout address':'Adresse de paiement','not a valid Bitcoin address':'adresse Bitcoin invalide',
'Device':'Appareil','night mode':'mode nuit','last reset':'dernier redémarrage','WiFi':'WiFi','Miners':'Miners','All miners':'Tous les miners',
'miners':'miners','no answer':'pas de réponse','online':'en ligne','ONLINE':'EN LIGNE','OFFLINE':'HORS LIGNE','NO CONNECTION':'PAS DE CONNEXION',
'Updated':'Mis à jour','market data':'données marché','min old':'min','mining paused':'minage en pause','power limited':'puissance limitée','battery saver: half speed':'économie de batterie : mi-régime',
'Recent blocks':'Derniers blocs','mining now':'en cours de minage','your best':'ta meilleure','ago':'il y a','Firmware update':'Mise à jour du firmware',
'Update available':'Mise à jour disponible','Install':'Installer','up to date':'à jour',
'Save and restart':'Enregistrer et redémarrer','Backup':'Sauvegarde','Download a backup':'Télécharger une sauvegarde','Restore a backup':'Restaurer une sauvegarde',
'Update':'Mettre à jour','Not a backup file':'Ce n\'est pas un fichier de sauvegarde','Restore this backup? The miner restarts afterwards.':'Restaurer cette sauvegarde ? Le miner redémarre ensuite.',
'Saved, restarting… reload in 15 s':'Enregistré, redémarrage… recharge dans 15 s','Upload failed':'Échec de l\'envoi',
'Refreshes every 5 s. Times are since the last start.':'Actualisé toutes les 5 s. Heures depuis le dernier démarrage.',
'backup_help':'Le fichier de sauvegarde contient tous les réglages (mots de passe compris : garde-le privé) et les statistiques à vie : meilleure difficulté, les 10 meilleures shares, hashes, shares, blocs, durée de fonctionnement. Le restaurer sur un autre miner lui donne aussi ces statistiques.',
'Display':'Affichage','Notifications':'Notifications','Safety':'Sécurité','Network':'Réseau'};
// Settings labels by key.
const FR_SET={pool:'Hôte de la pool',port:'Port de la pool',addr:'Adresse BTC (éventuellement adresse.worker)',pass:'Mot de passe de la pool',
diff:'Difficulté de share suggérée',ua:'User agent (pool.nerdminers.org demande NerdMinerV2)',pool2:'Pool de secours (vide = aucune)',port2:'Port de la pool de secours',
rotateh:'Alterner pool principale / secours toutes les N heures (0 = seulement en cas de panne)',bright:'Luminosité (%)',rot:'Rotation (0-3)',
autorot:'Rotation avec l\'accéléromètre (appareils M5)',lang:'Langue : 0 anglais, 1 français',theme:'Thème (0 orange, 1 vert, 2 bleu, 3 violet, 4 rose)',
cycle:'Écran suivant toutes les N secondes (0 = non)',nightfrom:'Mode nuit à partir de (heure 0-23, 255 = non)',nightto:'Mode nuit jusqu\'à (heure 0-23)',
beep:'Bip à chaque share 32 bits',tz:'Fuseau horaire (POSIX TZ, ex. CET-1CEST,M3.5.0,M10.5.0/3 ou UTC0)',fiat:'Devise du prix (USD, EUR, GBP, CAD, CHF, AUD, JPY)',
ntfy:'Sujet ntfy (vide = non)',ntfysrv:'Serveur ntfy',notify32:'Notifier aussi chaque share 32 bits',power:'Puissance de minage sur USB et batterie (0–100 %, 0 = pause)',maxtemp:'Pause du minage au-dessus de cette température en °C (0 = non)',
rebooth:'Redémarrer après N heures sans share acceptée (0 = non)',saver:'Économie de batterie : écran faible et minage à mi-régime sur batterie',
wifi2:'WiFi de secours 1 : nom',wpass2:'WiFi de secours 1 : mot de passe',wifi3:'WiFi de secours 2 : nom',wpass3:'WiFi de secours 2 : mot de passe',
mqtt:'Serveur MQTT pour Home Assistant (vide = non)',mqttport:'Port MQTT',mqttuser:'Utilisateur MQTT',mqttpass:'Mot de passe MQTT',
peers:'Autres miners du tableau de bord (IP ou noms, séparés par des virgules)',webpass:'Mot de passe web (réglages et mise à jour depuis le tableau de bord)',
update:'Mises à jour depuis GitHub : 0 non, 1 vérifier et proposer, 2 installer automatiquement',
count:'Compteur anonyme d\'utilisation : une fois par jour, télécharge un fichier de 2 octets de la release de cette version (rien n\'est envoyé)'};
function t(s){return LANG=='fr'&&FR[s]||s}

Object.assign(FR,{
 'Connection failed. Please try again.':'Connexion impossible. Réessaie.',
 'Request timed out. Please try again.':'Le délai de réponse est dépassé. Réessaie.',
 'Invalid response from the miner.':'Réponse du miner invalide.',
 'Authentication required. Reload this page and sign in.':'Authentification requise. Recharge la page et reconnecte-toi.',
 'Loading settings…':'Chargement des réglages…','Retry':'Réessayer',
 'Enter a valid value.':'Saisis une valeur valide.',
 'The temperature must be 0 or at least 30 °C.':'La température doit être 0 ou au moins 30 °C.',
 'The hour must be 0–23 or 255 to disable night mode.':'L’heure doit être comprise entre 0 et 23, ou 255 pour désactiver le mode nuit.',
 'The difficulty must be greater than zero.':'La difficulté doit être supérieure à zéro.',
 'Backup restored. The miner is restarting…':'Sauvegarde restaurée. Le miner redémarre…',
 'Select a firmware file first.':'Sélectionne d’abord un fichier de firmware.',
 'Use a firmware .bin file, not a factory image.':'Utilise le fichier firmware .bin, pas l’image factory.',
 'Invalid firmware file.':'Fichier de firmware invalide.',
 'Upload complete. Validating firmware…':'Envoi terminé. Validation du firmware…',
 'Firmware updated. The miner is restarting…':'Firmware mis à jour. Le miner redémarre…',
 'Upload timed out. Please try again.':'Le délai d’envoi est dépassé. Réessaie.',
 'Upload cancelled.':'Envoi annulé.',
 'Installing…':'Installation…','Installation requested.':'Installation demandée.',
 'GitHub updates':'Mises à jour GitHub','Check now':'Vérifier maintenant',
 'Checking…':'Vérification…','Check requested.':'Vérification demandée.',
 'Updates disabled in settings.':'Mises à jour désactivées dans les réglages.',
 'Waiting for the first check.':'En attente de la première vérification.',
 'Latest release checked.':'Dernière release vérifiée.',
 'Could not refresh the log.':'Impossible d’actualiser le journal.'
});
Object.assign(FR,{
 'Unsupported backup version':'Version de sauvegarde non prise en charge',
 'Invalid backup settings':'Réglages de sauvegarde invalides',
 'Missing backup statistics':'Statistiques absentes de la sauvegarde',
 'Invalid or incomplete backup statistics':'Statistiques de sauvegarde invalides ou incomplètes',
 'Invalid text setting':'Valeur de texte invalide',
 'Setting outside its allowed range':'Valeur hors de la plage autorisée',
 'Temperature must be 0 or at least 30 C':'La température doit être 0 ou au moins 30 °C',
 'Night hours must be 0-23 or 255':'Les heures doivent être comprises entre 0 et 23, ou 255',
 'Difficulty must be a finite positive number':'La difficulté doit être un nombre positif valide',
 'Invalid checkbox setting':'Valeur de case à cocher invalide',
 'Pool host and payout address cannot be empty':'L’hôte de la pool et l’adresse de paiement sont obligatoires',
 'No settings to save':'Aucun réglage à enregistrer',
 'No firmware file uploaded':'Aucun fichier de firmware envoyé',
 'Only one firmware file is allowed':'Un seul fichier de firmware est autorisé',
 'Empty firmware file':'Le fichier de firmware est vide',
 'Firmware update or restart pending; try again afterwards':'Une mise à jour ou un redémarrage est en cours. Réessaie ensuite'
});
function errorText(e){return e&&e.message?e.message:t('Connection failed. Please try again.')}
async function request(url,options={},format='json'){
 const {timeout=8000,...init}=options,c=new AbortController();
 const timer=setTimeout(()=>c.abort(),timeout);
 try{
  const r=await fetch(url,{cache:'no-store',...init,signal:c.signal});
  const body=await r.text();
  if(r.status===401)throw Error(t('Authentication required. Reload this page and sign in.'));
  if(!r.ok)throw Error(t(body)||'HTTP '+r.status);
  if(format==='text')return body;
  try{return JSON.parse(body)}catch(e){throw Error(t('Invalid response from the miner.'))}
 }catch(e){
  if(e.name==='AbortError')throw Error(t('Request timed out. Please try again.'));
  if(e instanceof TypeError)throw Error(t('Connection failed. Please try again.'));
  throw e;
 }finally{clearTimeout(timer)}
}

document.addEventListener('DOMContentLoaded',()=>{document.documentElement.lang=LANG;document.querySelectorAll('[data-t]').forEach(e=>e.textContent=t(e.dataset.t))});
</script>)JS";

const char kHeader[] PROGMEM = R"HTML(<header><div class="coin">B</div><h1>BDLMiner <b>V1</b></h1>
<nav><a href="/" data-t="Dashboard">Dashboard</a><a href="/settings" data-t="Settings">Settings</a><a href="/log" data-t="Log">Log</a><span id="st"></span></nav></header>)HTML";

const char kDashboard[] PROGMEM = R"HTML(<main id="m"></main>
<h2 id="ph" hidden data-t="Miners">Miners</h2><div class="grid" id="p"></div>
<footer id="f"></footer>
<script>
const SI=['','k','M','G','T','P','E','Z','Y'];
function si(v,u=''){if(v==null||!Number.isFinite(v))return'-';if(v===0)return'0'+(u?' '+u:'');let i=0;while(Math.abs(v)>=1000&&i<SI.length-1){v/=1000;i++}return v.toFixed(v<10?2:v<100?1:0)+' '+SI[i]+u}
function diff(d){return!d?'-':d<1?d.toFixed(5):si(d)}
function dur(s){if(!Number.isFinite(s))return'-';s=Math.max(0,s);if(s<60)return Math.round(s)+'s';const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;return(d?d+(LANG=='fr'?'j ':'d '):'')+(d||h?h+'h ':'')+m+'m'}
function odds(hr,nd,s){if(!hr||!nd)return'-';const p=-Math.expm1(-hr*s/(nd*4294967296));return(LANG=='fr'?'1 sur ':'1 in ')+si(1/p).replace(' ','')}
function chart(h={}){const v=Array.isArray(h.hashrate)?h.hashrate.filter(Number.isFinite):[],t0=tile(t('Hashrate history'),undefined,v.length?t('5-min averages, last')+' '+dur(v.length*h.interval_s)+' · '+t('avg')+' '+si(v.reduce((a,b)=>a+b,0)/v.length,'H/s')+(v.length>1?' · '+t('range')+' '+si(Math.min(...v))+'–'+si(Math.max(...v),'H/s'):''):t('first point after')+' '+h.interval_s/60+' '+t('min of mining'),true);
 if(v.length>1){const N='http://www.w3.org/2000/svg',W=300,H=60,lo=Math.min(...v),hi=Math.max(...v),k=(H-4)/((hi-lo)||hi||1),s=document.createElementNS(N,'svg'),p=document.createElementNS(N,'polyline');
  s.setAttribute('viewBox','0 0 '+W+' '+H);s.setAttribute('preserveAspectRatio','none');s.style.cssText='display:block;width:100%;height:60px;margin:6px 0';
  p.setAttribute('points',v.map((y,i)=>(i*W/(v.length-1)).toFixed(1)+','+(H-2-(y-lo)*k).toFixed(1)).join(' '));
  p.setAttribute('vector-effect','non-scaling-stroke');p.style.cssText='fill:none;stroke:var(--accent);stroke-width:2';
  s.appendChild(p);t0.insertBefore(s,t0.lastChild)}return t0}
function tile(label,value,sub,big){const e=document.createElement('div');e.className='t'+(big?' big':'');
 for(const[c,x]of[['l',label],['v',value],['s',sub]])if(x!==undefined){const d=document.createElement('div');d.className=c;d.textContent=x;e.appendChild(d)}return e}

let installPending=false,installRequestId=0,installError='',checkPending=false;
function upd(u){
 if(!u)return;
 if(installPending&&(u.request_id||0)!==installRequestId)installPending=false;
 const status=u.mode===0?'Updates disabled in settings.':u.checking?'Checking…':u.checked?'Latest release checked.':'Waiting for the first check.';
 const e=tile(t(u.available?'Update available':'GitHub updates'),u.version||'-',u.error||u.notes||installError||t(status),true);
 if(u.available){
 const b=document.createElement('button');b.className='gh-install';
 b.type='button';b.disabled=installPending||u.installing||u.checking;b.textContent=t(u.checking?'Checking…':b.disabled?'Installing…':'Install');
 b.onclick=async()=>{
  if(installPending||u.installing||u.checking)return;
  installRequestId=u.request_id||0;installPending=true;installError='';b.disabled=true;b.textContent=t('Installing…');
  try{await request('/api/github-update',{method:'POST'},'text');installError=t('Installation requested.')}
  catch(err){installPending=false;installError=errorText(err);b.disabled=false;b.textContent=t('Install')}
  e.querySelector('.s').textContent=installError;
 };
 e.appendChild(b);
 }
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
  const p=await request(u.origin+'/api/stats',{timeout:2500});
  if(!p||!Number.isFinite(p.hashrate)||p.hashrate<0||!p.pool||typeof p.pool.connected!=='boolean'||!p.shares||!Number.isFinite(p.shares.accepted)||!p.best||!Number.isFinite(p.best.ever))throw Error('Invalid peer');
  return p;
 }catch(e){return null}
}
async function tick(){
 try{const j=await request('/api/stats');const m=j.market,pv=j.pool_view;
  if(!m||!pv||!j.pool||!Array.isArray(j.engines))throw Error(t('Invalid response from the miner.'));
  document.documentElement.style.setProperty('--accent',j.accent);
  const st=document.getElementById('st');st.textContent=t(j.pool.connected?'ONLINE':'OFFLINE');st.style.color=j.pool.connected?'var(--ok)':'var(--bad)';
  const state=j.throttle=='paused'?'⚠ '+t('mining paused'):j.throttle=='half'?t('battery saver: half speed'):j.throttle=='reduced'?t('power limited'):'';
  const T=[tile(t('Hashrate'),si(j.hashrate,'H/s'),j.engines.map(e=>e.name+' '+si(e.rate,'H/s')).join(' + ')+(state?'\n'+state+' · '+j.power_percent+' %':''),true),
   tile(t('Shares'),j.shares.accepted+(j.shares.rejected?' / '+j.shares.rejected+' '+t('rejected'):''),t('pool diff')+' '+diff(j.pool.difficulty)),
   tile(t('Best difficulty'),diff(j.best.session),t('best ever')+' '+diff(j.best.ever)),
   tile(t('32-bit shares'),j.over32bits,t('1 every')+' '+(j.hashrate?dur(4294967296/j.hashrate):'-')+' '+t('on average')),
   tile(t('Block chance'),odds(j.hashrate,j.network.difficulty,86400)+' / '+t('day'),odds(j.hashrate,j.network.difficulty,31557600)+' / '+t('year')),
   tile(t('Blocks found'),j.blocks,t('lifetime')+' '+j.lifetime.blocks),
   tile(t('Total hashes'),si(j.total_hashes),t('uptime')+' '+dur(j.uptime_s)),
   tile(t('Block height'),j.network.height||'-',t('net diff')+' '+diff(j.network.difficulty)),
   tile(t('Lifetime'),si(j.lifetime.hashes)+' '+t('hashes'),j.lifetime.shares+' '+t('shares')+' · '+t('running')+' '+dur(j.lifetime.seconds)),
   chart(j.history||{})];
  const u=upd(j.github_update);if(u)T.unshift(u);
  if(pv.updated_s>=0)T.push(tile(t('Seen by the pool'),si(pv.hashrate,'H/s'),t('best')+' '+diff(pv.best)+' · '+pv.workers+' '+t('worker(s)')+' · '+Math.round(pv.updated_s/60)+' '+t('min ago')));
  else if(!pv.supported)T.push(tile(t('Seen by the pool'),'-',t('no stats API known for this pool')));
  const now=Date.now()/1e3,B=j.blocks_recent||[];
  if(B.length)T.push(tile(t('Recent blocks'),B[0].mining?B[0].h+' · '+t('mining now'):B[0].h,B.filter(b=>!b.mining).slice(0,6).map(b=>b.h+' · '+(b.pool||'?')+' · '+(LANG=='fr'?t('ago')+' '+dur(now-b.t):dur(now-b.t)+' '+t('ago'))+' · '+b.tx+' tx'+(b.best!=null?' · '+t('your best')+' '+diff(b.best):'')).join('\n')+(B[0].mining&&B[0].best!=null?'\n'+t('mining now')+': '+t('your best')+' '+diff(B[0].best):''),true));
  if(j.top&&j.top.length)T.push(tile(t('Best shares'),j.top.slice(0,5).map(x=>diff(x.d)).join('  '),j.top.slice(0,5).map(x=>x.t?new Date(x.t*1e3).toLocaleDateString():'-').join(' · '),true));
  if(m.updated_s>=0){T.push(tile(t('BTC price'),m.price?Math.round(m.price).toLocaleString()+' '+m.currency:'-',m.price?Math.round(1e8/m.price).toLocaleString()+' '+t('sats')+' / '+m.currency:'',true),
   tile(t('Fees sat/vB'),m.fees.join(' / '),t('fast / 30 min / 1 h')),
   tile(t('Network hashrate'),si(m.hashrate,'H/s'),m.mempool_tx.toLocaleString()+' '+t('tx in mempool')),
   tile(t('Next retarget'),(m.retarget.change>=0?'+':'')+m.retarget.change.toFixed(2)+' %',t('in')+' '+m.retarget.blocks+' '+t('blocks')+' ('+m.retarget.progress.toFixed(0)+' % '+t('done')+')'))}
  T.push(tile(t(j.pool.backup?'Pool (backup)':'Pool'),j.pool.host+':'+j.pool.port,j.pool.status),
   tile(t('Payout address'),j.address,j.address_type==='invalid'?'⚠ '+t('not a valid Bitcoin address'):j.address_type),
   tile(t('Device'),j.board,'v'+j.firmware+(j.temp_c==null?'':' · '+j.temp_c+' °C')+' · '+Math.round(j.heap/1024)+' KB'+(j.night?' · '+t('night mode'):'')+'\n'+t('last reset')+': '+j.boot_reason),
   tile(t('WiFi'),j.ip,j.ssid+' · '+j.rssi+' dBm'));
  document.getElementById('m').replaceChildren(...T);
  document.getElementById('f').textContent=t('Updated')+' '+new Date().toLocaleTimeString()+(m.updated_s>=0?' · '+t('market data')+' '+Math.round(m.updated_s/60)+' '+t('min old')+' (mempool.space)':'');

  const hosts=peerHosts(j.peers,j.ip);
  document.getElementById('ph').hidden=!hosts.length;
  if(hosts.length){
   const replies=await Promise.all(hosts.map(peer));let total=j.hashrate;
   const P=hosts.map((u,i)=>{const p=replies[i];if(!p)return tile(u.host,'-',t('no answer'));total+=p.hashrate;return tile(u.host,si(p.hashrate,'H/s'),(p.pool.connected?t('online'):t('OFFLINE'))+' · '+p.shares.accepted+' '+t('shares')+' · '+t('best')+' '+diff(p.best.ever))});
   P.unshift(tile(t('All miners'),si(total,'H/s'),(hosts.length+1)+' '+t('miners'),true));document.getElementById('p').replaceChildren(...P);
  }else document.getElementById('p').replaceChildren();
 }catch(e){const st=document.getElementById('st');st.textContent=t('NO CONNECTION');st.style.color='var(--bad)';document.getElementById('f').textContent=errorText(e)}
 finally{setTimeout(tick,3000)}
}
tick();

</script>)HTML";

const char kSettingsPage[] PROGMEM = R"HTML(<form id="f" class="box" onsubmit="saveSettings(event);return false"></form>
<div class="box"><button id="save" type="submit" form="f" disabled data-t="Save and restart">Save and restart</button>
<button id="retry" type="button" onclick="load()" hidden data-t="Retry">Retry</button>
<fieldset><legend data-t="Backup">Backup</legend><p class="s" id="bh">The backup file holds every setting (passwords included: keep it private) and the lifetime statistics: best difficulty, the 10 best shares, hashes, shares, blocks, running time. Restoring it on another miner also gives that one these statistics.</p>
<button id="backup" type="button" onclick="location.href='/api/backup'" disabled data-t="Download a backup">Download a backup</button>
<button id="restore" type="button" onclick="document.getElementById('imp').click()" disabled data-t="Restore a backup">Restore a backup</button><input type="file" id="imp" accept=".json" hidden onchange="imp(this)"></fieldset>
<fieldset><legend data-t="Firmware update">Firmware update</legend><input type="file" id="fw" accept=".bin" onchange="sync()"><button id="upload" type="button" onclick="uploadFirmware()" disabled data-t="Update">Update</button><div id="up" class="s" role="status" aria-live="polite"></div></fieldset>
<p class="s" id="msg" role="status" aria-live="polite"></p></div>
<script>
let S=null,busy=false,restarting=false;const $=id=>document.getElementById(id);
if(LANG=='fr')$('bh').textContent=t('backup_help');
function sync(){
 const off=busy||restarting||!S;
 for(const id of ['save','backup','restore'])$(id).disabled=off;
 $('upload').disabled=off||!$('fw').files.length;
 $('retry').disabled=busy||restarting;
 document.querySelectorAll('#f input,#fw,#imp').forEach(i=>i.disabled=busy||restarting);
}
async function load(){
 if(busy||restarting)return;
 busy=true;S=null;sync();$('msg').textContent=t('Loading settings…');$('retry').hidden=true;
 try{
  const data=await request('/api/settings');
  if(!data||!Array.isArray(data.schema)||!data.values)throw Error(t('Invalid response from the miner.'));
  const f=$('f');f.replaceChildren();let g='',fs;
  for(const[k,type,group,label,limits]of data.schema){
   if(group!=g){g=group;fs=document.createElement('fieldset');const lg=document.createElement('legend');lg.textContent=t(g);fs.appendChild(lg);f.appendChild(fs)}
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
   i.addEventListener('input',()=>i.setCustomValidity(''));fs.append(l,i);
  }
  S=data;$('msg').textContent='';
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
</script>)HTML";

const char kLogPage[] PROGMEM = R"HTML(<pre id="l">…</pre><footer data-t="Refreshes every 5 s. Times are since the last start.">Refreshes every 5 s. Times are since the last start.</footer>
<p id="log-status" class="box s" role="status" aria-live="polite"></p>
<script>
async function refreshLog(){
 try{document.getElementById('l').textContent=await request('/api/log',{},'text');document.getElementById('log-status').textContent=''}
 catch(e){document.getElementById('log-status').textContent=t('Could not refresh the log.')+' '+errorText(e)}
 finally{setTimeout(refreshLog,5000)}
}
refreshLog();
</script>)HTML";

void sendPage(const char *title, const char *body) {
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    server.sendContent(String("<!doctype html><html lang=\"en\"><head><title>") + title + "</title>");
    server.sendContent_P(kStyle);
    server.sendContent(String("<script>const LANG='") + (settings.language == 1 ? "fr" : "en") + "';</script>");
    server.sendContent_P(kI18n);
    server.sendContent("</head><body>");
    server.sendContent_P(kHeader);
    server.sendContent_P(body);
    server.sendContent("</body></html>");
    server.sendContent("");
}

// Settings and firmware need the web password (set in the setup portal).
bool authorized() {
    if (settings.webPassword.isEmpty()) {
        server.send(403, "text/plain",
                    "Set a web password in the setup portal first (hold the main button while the miner starts).");
        return false;
    }
    if (!server.authenticate("admin", settings.webPassword.c_str())) {
        server.requestAuthentication(BASIC_AUTH, "BDLMiner", "Wrong password");
        return false;
    }
    return true;
}

void restartSoon(const char *why) {
    restartWhy = why;
    restartAt = millis() + 1500;
}

const char *accentCss() {
    static const char *kAccent[] = {"#f7931a", "#38d07a", "#3b9cf6", "#a66bf5", "#ec5aa0"};
    return kAccent[settings.theme % 5];
}

void handleStats() {
    const MinerStats stats = minerStats();
    JsonDocument j;
    j["board"] = board::modelName();
    j["firmware"] = FW_VERSION;
    j["uptime_s"] = minerUptimeSeconds();
    j["hashrate"] = minerHashrate();
    j["total_hashes"] = (double)minerTotalHashes();
    JsonArray engines = j["engines"].to<JsonArray>();
    for (int i = 0; i < stats.minerCount; i++) {
        JsonObject e = engines.add<JsonObject>();
        e["name"] = stats.engine[i];
        e["rate"] = minerEngineRate(i);
    }
    j["shares"]["accepted"] = stats.accepted;
    j["shares"]["rejected"] = stats.rejected;
    j["best"]["session"] = stats.sessionBestDiff;
    j["best"]["ever"] = stats.bestDiff;
    j["blocks"] = stats.blocksFound;
    j["over32bits"] = stats.over32bits;
    BestShare top[TOP_SHARES];
    int nt = minerTopShares(top, TOP_SHARES);
    JsonArray ta = j["top"].to<JsonArray>();
    for (int i = 0; i < nt; i++) {
        JsonObject o = ta.add<JsonObject>();
        o["d"] = top[i].difficulty;
        o["t"] = top[i].time;
    }
    j["pool"]["host"] = netPoolHost();
    j["pool"]["port"] = netPoolPort();
    j["pool"]["backup"] = netOnBackup();
    j["pool"]["connected"] = stats.poolConnected;
    j["pool"]["status"] = netStatus();
    j["pool"]["difficulty"] = stats.poolDiff;
    PoolStats pv = poolStatsGet();
    j["pool_view"]["updated_s"] = pv.updatedMs ? (long)((millis() - pv.updatedMs) / 1000) : -1;
    j["pool_view"]["supported"] = pv.supported;
    j["pool_view"]["hashrate"] = pv.hashrate;
    j["pool_view"]["best"] = pv.bestEver;
    j["pool_view"]["shares"] = pv.shares;
    j["pool_view"]["workers"] = pv.workers;
    j["network"]["height"] = stats.blockHeight;
    j["network"]["difficulty"] = stats.networkDiff;
    j["address"] = settings.btcAddress;
    j["address_type"] = address::typeName(address::parse(settings.btcAddress.c_str()).type);
    j["lifetime"]["hashes"] = (double)stats.lifetimeHashes;
    j["lifetime"]["shares"] = stats.lifetimeShares;
    j["lifetime"]["blocks"] = stats.lifetimeBlocks;
    j["lifetime"]["seconds"] = stats.lifetimeSeconds;
    static float hist[HISTORY_LEN];
    int n = minerHistory(hist, HISTORY_LEN);
    j["history"]["interval_s"] = HISTORY_INTERVAL_S;
    JsonArray hr = j["history"]["hashrate"].to<JsonArray>();
    for (int i = 0; i < n; i++) hr.add((long)hist[i]);
    j["heap"] = ESP.getFreeHeap();
    j["heap_max_block"] = ESP.getMaxAllocHeap();
    float temp = board::temperature();
    j["temperature_source"] = board::temperatureSource();
    if (isnan(temp)) j["temp_c"] = nullptr;
    else j["temp_c"] = (int)lroundf(temp);
    j["throttle"] = minerThrottle() == Throttle::Paused ? "paused" : minerThrottle() == Throttle::Half ? "half" : "full";
    j["power_percent"] = minerPowerPercent();
    j["power_limit_percent"] = settings.miningPower;
    if (minerThrottle() == Throttle::Reduced) j["throttle"] = "reduced";
    j["night"] = watchNight();
    j["boot_reason"] = bootReason();
    j["accent"] = accentCss();
    j["ip"] = WiFi.localIP().toString();
    j["ssid"] = WiFi.SSID();
    j["rssi"] = WiFi.RSSI();
    JsonArray peers = j["peers"].to<JsonArray>();
    for (int from = 0; from < (int)settings.peers.length();) {
        int comma = settings.peers.indexOf(',', from);
        if (comma < 0) comma = settings.peers.length();
        String p = settings.peers.substring(from, comma);
        p.trim();
        if (p.length()) peers.add(p);
        from = comma + 1;
    }

    MarketData m = marketGet();
    JsonObject mk = j["market"].to<JsonObject>();
    mk["updated_s"] = m.updatedMs ? (long)((millis() - m.updatedMs) / 1000) : -1;
    mk["currency"] = settings.currency;
    mk["price"] = m.price;
    JsonArray fees = mk["fees"].to<JsonArray>();
    fees.add(m.feeFast);
    fees.add(m.feeHalfHour);
    fees.add(m.feeHour);
    mk["hashrate"] = m.networkHashrate;
    mk["mempool_tx"] = m.mempoolTx;
    mk["retarget"]["change"] = m.retargetChange;
    mk["retarget"]["progress"] = m.retargetProgress;
    mk["retarget"]["blocks"] = m.retargetBlocks;
    // Latest blocks, with your best share while each was the one to find
    // (the first entry is the block being mined, not found yet).
    BlockBest bb[BLOCK_BESTS];
    int nb = minerBlockBests(bb, BLOCK_BESTS);
    auto bestFor = [&](uint32_t h, JsonObject o) {
        for (int i = 0; i < nb; i++)
            if (bb[i].height == h) o["best"] = bb[i].best;
    };
    JsonArray blocks = j["blocks_recent"].to<JsonArray>();
    if (stats.blockHeight) {
        JsonObject o = blocks.add<JsonObject>();
        o["h"] = stats.blockHeight;
        o["mining"] = true;
        bestFor(stats.blockHeight, o);
    }
    for (int i = 0; i < m.blockCount; i++) {
        JsonObject o = blocks.add<JsonObject>();
        o["h"] = m.blocks[i].height;
        o["t"] = m.blocks[i].time;
        o["pool"] = m.blocks[i].pool;
        o["tx"] = m.blocks[i].txs;
        bestFor(m.blocks[i].height, o);
    }
    j["lang"] = settings.language == 1 ? "fr" : "en";
    GithubUpdate gu = ghUpdateState();
    JsonObject up = j["github_update"].to<JsonObject>();
    up["available"] = gu.available;
    up["mode"] = settings.githubUpdate;
    up["checked"] = gu.checked;
    up["checking"] = gu.checking;
    up["request_id"] = gu.installRequest;
    up["version"] = gu.version;
    up["installing"] = gu.installing;
    up["progress"] = gu.progress;
    up["error"] = gu.error;
    up["notes"] = gu.installing ? String(gu.progress) + " %" : String(gu.error);

    String body;
    serializeJson(j, body);
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Access-Control-Allow-Origin", "*");  // read by the other miners' dashboards
    server.send(200, "application/json", body);
}

// Every setting's value, under its key.
void settingValues(JsonObject values) {
    Settings visible = settings;
    visible.brightness = settingsBrightness();
    for (size_t i = 0; i < kSettingCount; i++) {
        const SettingInfo &s = kSettings[i];
        const void *value = settingValue(s, visible);
        switch (s.type) {
            case SettingType::Text:
            case SettingType::Secret: values[s.key] = *(const String *)value; break;
            case SettingType::Number:
                values[s.key] = s.bits == 8 ? *(const uint8_t *)value : *(const uint16_t *)value;
                break;
            case SettingType::Decimal: values[s.key] = *(const double *)value; break;
            case SettingType::Bool: values[s.key] = *(const bool *)value; break;
        }
    }
}

// Stage, normalize and save known keys; -1 means a required field became empty.
int applySettings(JsonObjectConst values) {
    // Network tasks keep using immutable live strings until the reboot.
    Settings pending = settings;
    pending.brightness = settingsBrightness();
    int changed = settingsApply(pending, values);
    if (changed) {
        settingsSanitize(pending);
        if (pending.poolHost.isEmpty() || pending.btcAddress.isEmpty()) return -1;
        settingsSave(pending, true);
    }
    return changed;
}

void handleGetSettings() {
    if (!authorized()) return;
    static const char *kTypes[] = {"text", "secret", "number", "decimal", "bool"};
    JsonDocument j;
    JsonArray schema = j["schema"].to<JsonArray>();
    for (size_t i = 0; i < kSettingCount; i++) {
        const SettingInfo &s = kSettings[i];
        JsonArray row = schema.add<JsonArray>();
        row.add(s.key);
        row.add(kTypes[(int)s.type]);
        row.add(s.group);
        row.add(s.label);
        JsonObject limits = row.add<JsonObject>();
        if (s.type == SettingType::Number) {
            SettingLimits range = settingLimits(s);
            limits["min"] = range.minimum;
            limits["max"] = range.maximum;
        } else if (s.type == SettingType::Decimal) limits["min"] = 0;
    }
    settingValues(j["values"].to<JsonObject>());
    String body;
    serializeJson(j, body);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", body);
}

// Saves the settings present in the JSON body (the page's form) and restarts,
// which applies everything the same way as at boot.
void handlePostSettings() {
    if (!authorized()) return;
    if (restartAt || ota::busy()) {
        server.send(409, "text/plain", "Firmware update or restart pending; try again afterwards");
        return;
    }
    JsonDocument j;
    if (deserializeJson(j, server.arg("plain")) || !j.is<JsonObject>()) {
        server.send(400, "text/plain", "Not a settings object");
        return;
    }
    if (const char *error = settingsError(j.as<JsonObjectConst>())) {
        server.send(400, "text/plain", error);
        return;
    }
    int changed = applySettings(j.as<JsonObjectConst>());
    if (changed < 0) {
        server.send(400, "text/plain", "Pool host and payout address cannot be empty");
        return;
    }
    if (!changed) {
        server.send(400, "text/plain", "No settings to save");
        return;
    }
    logf("[web] %d settings saved, restarting\n", changed);
    server.send(200, "text/plain", "Saved, restarting");
    restartSoon("settings changed from the web page");
}

// GET /api/backup: settings (passwords included) and lifetime statistics.
void handleGetBackup() {
    if (!authorized()) return;
    JsonDocument j;
    j["bdlminer_backup"] = 1;
    j["board"] = board::modelName();
    j["firmware"] = FW_VERSION;
    time_t now = time(nullptr);
    j["date"] = now > 1700000000 ? (uint32_t)now : 0;
    settingValues(j["settings"].to<JsonObject>());
    StatsSnapshot s;
    minerStatsSnapshot(s);
    JsonObject st = j["stats"].to<JsonObject>();
    st["best"] = s.bestDiff;
    st["hashes"] = s.hashes;
    st["shares"] = s.shares;
    st["blocks"] = s.blocks;
    st["seconds"] = s.seconds;
    JsonArray top = st["top"].to<JsonArray>();
    for (const BestShare &b : s.top) {
        if (!(b.difficulty > 0)) break;
        JsonObject o = top.add<JsonObject>();
        o["d"] = b.difficulty;
        o["t"] = b.time;
    }
    String body;
    serializeJson(j, body);
    char name[80];
    snprintf(name, sizeof(name), "attachment; filename=\"%s-backup.json\"", hostName.c_str());
    server.sendHeader("Content-Disposition", name);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", body);
}

// POST /api/backup: a file from GET /api/backup (or a settings-only export
// of an earlier version); restores what it holds, then restarts.
void handlePostBackup() {
    if (!authorized()) return;
    if (restartAt || ota::busy()) {
        server.send(409, "text/plain", "Firmware update or restart pending; try again afterwards");
        return;
    }
    JsonDocument j;
    if (deserializeJson(j, server.arg("plain")) || !j.is<JsonObject>()) {
        server.send(400, "text/plain", "Not a backup file");
        return;
    }
    if (const char *error = webvalidation::backupError(j.as<JsonObjectConst>(), TOP_SHARES)) {
        server.send(400, "text/plain", error);
        return;
    }
    bool full = j["bdlminer_backup"].is<unsigned>();
    JsonObjectConst values = full ? j["settings"].as<JsonObjectConst>() : j.as<JsonObjectConst>();
    if (const char *error = settingsError(values)) {
        server.send(400, "text/plain", error);
        return;
    }
    int settingsCount = applySettings(values);
    if (settingsCount < 0) {
        server.send(400, "text/plain", "Pool host and payout address cannot be empty");
        return;
    }
    JsonObjectConst st = j["stats"];
    bool withStats = full && !st.isNull();
    if (withStats) {
        StatsSnapshot s{};
        s.bestDiff = st["best"] | 0.0;
        s.hashes = st["hashes"] | (uint64_t)0;
        s.shares = st["shares"] | 0u;
        s.blocks = st["blocks"] | 0u;
        s.seconds = st["seconds"] | 0u;
        int i = 0;
        for (JsonObjectConst o : st["top"].as<JsonArrayConst>()) {
            if (i == TOP_SHARES) break;
            s.top[i++] = {o["d"] | 0.0, o["t"] | 0u};
        }
        minerStatsRestore(s);
    }
    if (!settingsCount && !withStats) {
        server.send(400, "text/plain", "Nothing to restore in this file");
        return;
    }
    logf("[web] backup restored: %d settings%s, restarting\n", settingsCount, withStats ? " and statistics" : "");
    server.send(200, "text/plain", String("Restored ") + settingsCount + " settings" +
                                       (withStats ? " and the statistics" : "") + ", restarting… reload in 15 s");
    restartSoon("backup restored from the web page");
}

// POST /update (multipart): one firmware.bin built for this board.
bool uploadStarted = false, uploadComplete = false;
bool updateAllowed = false, updateBusy = false, uploadRejected = false;
String uploadError;

void resetUpload() {
    uploadStarted = uploadComplete = updateAllowed = updateBusy = uploadRejected = false;
    uploadError = "";
}

void abortWebUpload() {
    if (uploadStarted && updateAllowed && !restartAt && ota::owns(ota::Owner::Web)) {
        if (Update.isRunning()) Update.abort();
        ota::release(ota::Owner::Web);
    }
}

void uploadFailure(int status, const String &message) {
    abortWebUpload();
    server.send(status, "text/plain", message);
    resetUpload();
}

void handleUpload() {
    HTTPUpload &up = server.upload();
    if (up.status == UPLOAD_FILE_WRITE) vTaskDelay(1);
    if (up.status == UPLOAD_FILE_ABORTED) {
        abortWebUpload();
        resetUpload();
        return;
    }
    if (up.status == UPLOAD_FILE_START) {
        if (uploadStarted) {
            uploadRejected = true;
            uploadComplete = false;
            uploadError = "Only one firmware file is allowed";
            abortWebUpload();
            return;
        }
        resetUpload();
        uploadStarted = true;
        updateAllowed = !settings.webPassword.isEmpty() && server.authenticate("admin", settings.webPassword.c_str());
        if (!updateAllowed) return;
        if (restartAt) { updateBusy = true; return; }
        String name = up.filename;
        name.toLowerCase();
        if (!name.endsWith(".bin") || name.endsWith("factory.bin")) {
            uploadRejected = true;
            uploadError = "Use a firmware .bin file, not a factory image";
            return;
        }
        if (!ota::acquire(ota::Owner::Web)) { updateBusy = true; return; }
        logf("[web] firmware update: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            uploadError = Update.errorString();
            ota::release(ota::Owner::Web);
        }
        return;
    }
    if (!updateAllowed || updateBusy || uploadRejected || !ota::owns(ota::Owner::Web)) return;
    if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            uploadError = Update.errorString();
            abortWebUpload();
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (!up.totalSize) {
            uploadRejected = true;
            uploadError = "Empty firmware file";
            abortWebUpload();
        } else {
            // Activate the new image only after the entire multipart request
            // is parsed and authenticated. A later part or abort can still fail.
            uploadComplete = true;
        }
    }
}

void handleUpdateDone() {
    // Authenticate this request even if it has no multipart callbacks. Never
    // reuse authorization or success flags from an earlier upload.
    if (!authorized()) { abortWebUpload(); resetUpload(); return; }
    if (restartAt || updateBusy) {
        uploadFailure(409, "Another firmware update or restart is already pending");
        return;
    }
    if (!uploadStarted) { uploadFailure(400, "No firmware file uploaded"); return; }
    if (uploadRejected) { uploadFailure(400, uploadError); return; }
    if (!uploadComplete || !uploadError.isEmpty() || !ota::owns(ota::Owner::Web)) {
        String why = uploadError.isEmpty() ? String("Incomplete firmware upload") : uploadError;
        uploadFailure(500, String("Update failed: ") + why);
        return;
    }
    if (!Update.end(true)) {
        String why = Update.errorString();
        uploadFailure(500, String("Update failed: ") + why +
                          " (this board may require a USB update)");
        return;
    }
    logf("[web] firmware validated, restarting\n");
    resetUpload();
    server.send(200, "text/plain", "Firmware updated, restarting… reload in 20 s");
    restartSoon("firmware updated from the web page");
    // Keep OTA ownership until restart so neither miners nor another updater
    // can modify the newly selected image in the response/reboot interval.
}

// Prometheus text format, for Grafana / Home Assistant scrapers.
void handleMetrics() {
    const MinerStats stats = minerStats();
    String out;
    out.reserve(1536);
    auto metric = [&out](const char *name, const char *type, double value, const char *labels = "") {
        char line[160];
        if (type) {
            snprintf(line, sizeof(line), "# TYPE %s %s\n", name, type);
            out += line;
        }
        snprintf(line, sizeof(line), "%s%s %.17g\n", name, labels, value);
        out += line;
    };
    metric("bdlminer_hashrate_hps", "gauge", minerHashrate());
    metric("bdlminer_hashes_total", "counter", (double)minerTotalHashes());
    metric("bdlminer_shares_accepted_total", "counter", stats.accepted);
    metric("bdlminer_shares_rejected_total", "counter", stats.rejected);
    metric("bdlminer_blocks_found_total", "counter", stats.blocksFound);
    metric("bdlminer_best_difficulty", "gauge", stats.sessionBestDiff);
    metric("bdlminer_best_difficulty_ever", "gauge", stats.bestDiff);
    metric("bdlminer_shares_32bit_total", "counter", stats.over32bits);
    metric("bdlminer_lifetime_hashes_total", "counter", (double)stats.lifetimeHashes);
    metric("bdlminer_lifetime_shares_total", "counter", stats.lifetimeShares);
    metric("bdlminer_lifetime_seconds_total", "counter", stats.lifetimeSeconds);
    metric("bdlminer_pool_connected", "gauge", stats.poolConnected ? 1 : 0);
    metric("bdlminer_pool_backup", "gauge", netOnBackup() ? 1 : 0);
    metric("bdlminer_pool_difficulty", "gauge", stats.poolDiff);
    metric("bdlminer_block_height", "gauge", stats.blockHeight);
    metric("bdlminer_network_difficulty", "gauge", stats.networkDiff);
    metric("bdlminer_uptime_seconds", "gauge", minerUptimeSeconds());
    if (!isnan(board::temperature())) metric("bdlminer_temperature_celsius", "gauge", board::temperature());
    metric("bdlminer_throttle", "gauge", (int)minerThrottle());
    metric("bdlminer_power_percent", "gauge", minerPowerPercent());
    metric("bdlminer_wifi_rssi_dbm", "gauge", WiFi.RSSI());
    metric("bdlminer_free_heap_bytes", "gauge", ESP.getFreeHeap());
    for (int i = 0; i < stats.minerCount; i++) {
        char labels[40];
        // Software fallback can run two engines with the same name.
        snprintf(labels, sizeof(labels), "{engine=\"%s\",index=\"%d\"}", stats.engine[i], i);
        // A metric family has one TYPE line, even with several engines.
        metric("bdlminer_engine_hashrate_hps", i == 0 ? "gauge" : nullptr, minerEngineRate(i), labels);
    }
    char labels[96];
    snprintf(labels, sizeof(labels), "{board=\"%s\",firmware=\"%s\"}", board::modelName(), FW_VERSION);
    metric("bdlminer_info", "gauge", 1, labels);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain; version=0.0.4", out);
}

void webTask(void *) {
    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

void webStart() {
    char name[24];
    snprintf(name, sizeof(name), "bdlminer-%04x", (unsigned)((ESP.getEfuseMac() >> 32) & 0xFFFF));
    hostName = name;
    if (MDNS.begin(name)) MDNS.addService("http", "tcp", 80);

    server.on("/", HTTP_GET, []() { sendPage("BDLMiner V1", kDashboard); });
    server.on("/settings", HTTP_GET, []() {
        if (authorized()) sendPage("BDLMiner settings", kSettingsPage);
    });
    server.on("/log", HTTP_GET, []() { sendPage("BDLMiner log", kLogPage); });
    server.on("/api/stats", HTTP_GET, handleStats);
    server.on("/api/log", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "text/plain", logText());
    });
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/settings", HTTP_POST, handlePostSettings);
    server.on("/api/backup", HTTP_GET, handleGetBackup);
    server.on("/api/backup", HTTP_POST, handlePostBackup);
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpload);
    server.on("/api/github-check", HTTP_POST, []() {
        if (!settings.webPassword.isEmpty() && !authorized()) return;
        if (ghUpdateCheck()) server.send(202, "text/plain", "Check requested");
        else server.send(409, "text/plain", "GitHub updates are disabled or busy");
    });
    server.on("/api/github-update", HTTP_POST, []() {
        // Only installs the latest release, and only when it carries the
        // release key's signature: without a web password (read-only
        // dashboard) it is still allowed.
        if (!settings.webPassword.isEmpty() && !authorized()) return;
        if (ghUpdateInstall()) server.send(200, "text/plain", "Installing, the miner restarts by itself");
        else server.send(409, "text/plain", "No update to install");
    });
    server.on("/metrics", HTTP_GET, handleMetrics);
    server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
    server.begin();
    // Slow HTTP clients must never stop the loop that owns the I2C sensors,
    // thermal control, buttons and screen. The worker uses cached snapshots.
    if (xTaskCreatePinnedToCore(webTask, "web", 6144, nullptr, 2, nullptr, 0) != pdPASS) {
        server.stop();
        logf("[web] not enough memory to start the web task\n");
        return;
    }
    logf("[web] dashboard: http://%s/ or http://%s.local/\n", WiFi.localIP().toString().c_str(), name);
}

void webHandle() {
    if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
        minerSaveStats();  // does nothing after a restored backup
        setRestartReason(restartWhy.load());
        ESP.restart();
    }
}

const String &webHostName() { return hostName; }
