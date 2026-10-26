let logText='',logPaused=false;
const logView=document.getElementById('l'),logFilter=document.getElementById('log-search');
function renderLog(){const query=logFilter.value.toLocaleLowerCase();const lines=logText.split('\n').filter(line=>line.toLocaleLowerCase().includes(query));logView.textContent=lines.length?lines.join('\n'):t('No matching entries.')}
logFilter.addEventListener('input',renderLog);
document.getElementById('pause-log').onclick=()=>{logPaused=!logPaused;document.getElementById('pause-log').textContent=t(logPaused?'Resume':'Pause');if(!logPaused)logPoll.refresh()};
document.getElementById('download-log').onclick=()=>{const url=URL.createObjectURL(new Blob([logText],{type:'text/plain;charset=utf-8'})),a=document.createElement('a');a.href=url;a.download='bdlminer.log';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)};
async function refreshLog(){
 if(logPaused)return;
 try{logText=await request('/api/log',{},'text');renderLog();document.getElementById('log-status').textContent='';document.getElementById('log-time').textContent=new Date().toLocaleTimeString()}
 catch(e){document.getElementById('log-status').textContent=t('Could not refresh the log.')+' '+errorText(e)}
}
const logPoll=startPolling(refreshLog,5000);
