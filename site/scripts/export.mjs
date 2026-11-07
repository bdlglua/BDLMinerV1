// Copies the exported pages to installer/site/, where tools/make_site_pages.py
// reads them. Fails if Next.js left any runtime script or asset reference in a page.
import {readdirSync,readFileSync,writeFileSync,mkdirSync,rmSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
const here=dirname(fileURLToPath(import.meta.url)),out=join(here,'../out'),dest=join(here,'../../installer/site');
rmSync(dest,{recursive:true,force:true});mkdirSync(dest,{recursive:true});
const pages=readdirSync(out).filter(name=>name.endsWith('.html')&&name!=='404.html');
for(const name of pages){
 let html=readFileSync(join(out,name),'utf8');
 if(/_next\//.test(html))throw new Error(name+' references the Next.js runtime: keep pages static (unstable_runtimeJS:false)');
 html=html.replace(/ data-next-head=""/g,'').replace('<noscript data-n-css=""></noscript>','');
 writeFileSync(join(dest,name),html.replace(/(<\/head>|<body|<nav |<main|<footer|<section)/g,'\n$1')+'\n');
}
console.log('exported',pages.length,'pages to installer/site/');
