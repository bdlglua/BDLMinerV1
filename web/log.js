let logText='',logPaused=false,logCategory='all';
const logView=document.getElementById('l'),logFilter=document.getElementById('log-search'),byId=id=>document.getElementById(id);
// Log tags ("[pool]") grouped into the categories offered as filters.
const LOG_GROUPS=[['pool','Pool','#3b9cf6',['pool','sv2','stratum','backup']],['miner','Mining','#f7931a',['miner','stats','hw','asic','gamma','fill']],
 ['network','Network','#3ddc97',['wifi','net','tls','market','web','mdns','dns','time']],['updates','Updates','#a66bf5',['update','ota','github']],
 ['system','System','#8a96a8',['boot','crash','watch','cfg','loop','debug','i2c']],['integrations','Integrations','#ec5aa0',['notify','mqtt']]];
const BAD=/\b(?:error|failed|failure|crash|panic|abort|guru|refused|invalid|too hot|not delivered|stalled)\b|WARNING|Backtrace/i;
const WARN=/\b(?:retry|retrying|reconnect\w*|disconnected|timeout|timed out|throttl\w*|rolled back|offline|lost|fallback|paused)\b|HTTP [45]\d\d/i;
function parseLine(line){
 const m=/^(\d\d:\d\d:\d\d)\s(?:\[([\w.-]+)\]\s?)?(.*)$/.exec(line);
 const time=m?m[1]:'',tag=m&&m[2]?m[2]:'',rest=m?m[3]:line,lower=tag.toLowerCase();
 const group=LOG_GROUPS.find(g=>g[3].some(x=>lower.startsWith(x)))||(tag?LOG_GROUPS[4]:null);
 const level=BAD.test(rest)&&!/self-test OK/.test(rest)?'bad':WARN.test(rest)?'warn':'';
 return{line,time,tag,rest,group,level};
}
function seconds(time){const p=time.split(':').map(Number);return p.length===3?p[0]*3600+p[1]*60+p[2]:NaN}
function span(s){if(!(s>=0))return'–';if(s<60)return Math.round(s)+' s';if(s<3600)return Math.round(s/60)+' min';return Math.floor(s/3600)+' h '+String(Math.floor(s/60)%60).padStart(2,'0')}
// The text stays exactly the device's: lines joined by newlines, highlighted only.
function piece(cls,text,query){
 const el=document.createElement('span');el.className=cls;
 if(!query){el.textContent=text;return el}
 const lower=text.toLocaleLowerCase();let at=0,i;
 while((i=lower.indexOf(query,at))>=0){el.append(text.slice(at,i));const mark=document.createElement('mark');mark.textContent=text.slice(i,i+query.length);el.append(mark);at=i+query.length}
 el.append(text.slice(at));return el;
}
function renderTags(entries){
 const host=byId('log-tags'),counts=new Map([['all',entries.length],['alerts',entries.filter(e=>e.level).length]]);
 for(const e of entries)if(e.group)counts.set(e.group[0],(counts.get(e.group[0])||0)+1);
 const chips=[['all','All','currentColor'],['alerts','Warnings','var(--bad)'],...LOG_GROUPS.filter(g=>counts.get(g[0]))];
 if(!chips.some(c=>c[0]===logCategory))logCategory='all';
 host.replaceChildren(...chips.map(([id,name,color])=>{
  const b=document.createElement('button');b.type='button';b.className='chip';b.dataset.category=id;b.setAttribute('aria-pressed',id===logCategory);
  const dot=document.createElement('i');dot.style.color=color;const n=document.createElement('b');n.textContent=counts.get(id)||0;
  b.append(dot,t(name),n);b.onclick=()=>{logCategory=id;renderLog()};return b;
 }));
}
function renderSummary(entries){
 const times=entries.map(e=>seconds(e.time)).filter(Number.isFinite),alerts=entries.filter(e=>e.level==='bad').length+entries.filter(e=>e.level==='warn').length;
 byId('sum-lines').textContent=entries.length;
 const a=byId('sum-alerts');a.textContent=alerts;a.className=entries.some(e=>e.level==='bad')?'bad':'';
 byId('sum-span').textContent=times.length?span(times[times.length-1]-times[0]):'–';
 const reset=entries.find(e=>/^last reset:/i.test(e.rest));byId('sum-reset').textContent=reset?reset.rest.replace(/^last reset:\s*/i,''):'–';
 byId('sum-reset').title=byId('sum-reset').textContent;
}
function renderLog(){
 const query=logFilter.value.toLocaleLowerCase(),entries=logText?logText.split('\n').filter(l=>l.length).map(parseLine):[];
 renderTags(entries);renderSummary(entries);
 const shown=entries.filter(e=>(logCategory==='all'||(logCategory==='alerts'?e.level:e.group&&e.group[0]===logCategory))&&e.line.toLocaleLowerCase().includes(query));
 const follow=logView.scrollHeight-logView.scrollTop-logView.clientHeight<40;
 if(!shown.length){const empty=document.createElement('div');empty.className='log-empty';empty.textContent=logText?t('No matching entries.'):'…';logView.replaceChildren(empty)}
 else{
  const nodes=[];
  shown.forEach((e,n)=>{
   const row=document.createElement('div');row.className='log-line';if(e.level)row.dataset.level=e.level;
   if(e.time){row.append(piece('time',e.time,query),' ');const tag=piece('tag',e.tag?'['+e.tag+']':'',query);if(e.group)tag.style.setProperty('--tag',e.group[2]);row.append(tag,e.tag?' ':'');row.append(piece('msg',e.rest,query))}
   else row.append(piece('msg',e.line,query));
   nodes.push(row);if(n<shown.length-1)nodes.push('\n');
  });
  logView.replaceChildren(...nodes);
 }
 byId('log-count').textContent=shown.length===entries.length?'':shown.length+' / '+entries.length;
 if(follow)logView.scrollTop=logView.scrollHeight;
}
logFilter.addEventListener('input',renderLog);
byId('pause-log').onclick=()=>{
 logPaused=!logPaused;byId('pause-log').textContent=t(logPaused?'Resume':'Pause');
 byId('log-state').textContent=t(logPaused?'PAUSED':'LIVE');byId('log-panel').dataset.paused=logPaused;
 if(!logPaused)logPoll.refresh();
};
byId('download-log').onclick=()=>{const url=URL.createObjectURL(new Blob([logText],{type:'text/plain;charset=utf-8'})),a=document.createElement('a');a.href=url;a.download='bdlminer.log';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)};
async function refreshLog(){
 if(logPaused)return;
 try{const text=await request('/api/log',{},'text');if(logPaused)return;logText=text;renderLog();byId('log-status').textContent='';byId('log-time').textContent=new Date().toLocaleTimeString()}
 catch(e){if(!logPaused)byId('log-status').textContent=t('Could not refresh the log.')+' '+errorText(e)}
}
const logPoll=startPolling(refreshLog,5000);
