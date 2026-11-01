/* The IMU logger web page, served from flash. Self-contained (no CDN), since
 * the board's access point has no internet.
 */
#pragma once

#include <Arduino.h>

static const char indexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CADSE IMU logger</title>
<style>
:root{--bg:#f4f4f1;--card:#fff;--text:#1d1d1b;--muted:#6b6b66;--line:#e0dfda;--accent:#2563eb;--rec:#dc2626;--c1:#2563eb;--c2:#16a34a;--c3:#ea580c}
@media(prefers-color-scheme:dark){:root{--bg:#141413;--card:#1f1f1d;--text:#ecebe6;--muted:#9c9b94;--line:#34332f;--accent:#60a5fa;--rec:#f87171;--c1:#60a5fa;--c2:#4ade80;--c3:#fb923c}}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:var(--bg);color:var(--text)}
header{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:14px 16px;flex-wrap:wrap}
h1{font-size:18px;margin:0;font-weight:600}
main{display:grid;gap:12px;padding:0 16px 16px;grid-template-columns:repeat(auto-fit,minmax(320px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;min-width:0}
.card h2{font-size:13px;margin:0 0 10px;color:var(--muted);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
.vals{display:grid;grid-template-columns:auto repeat(3,1fr);gap:4px 10px;align-items:baseline;font-variant-numeric:tabular-nums}
.vals b{font-size:22px;font-weight:600;text-align:right}
.vals span{font-size:12px;color:var(--muted)}
canvas{width:100%;display:block}
button,select{font:inherit;font-size:14px;padding:7px 12px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--text);cursor:pointer}
button.primary{background:var(--accent);color:#fff;border-color:var(--accent)}
button.rec{background:var(--rec);color:#fff;border-color:var(--rec)}
table{width:100%;border-collapse:collapse;font-size:14px}
td{padding:7px 4px;border-top:1px solid var(--line)}
td.r{text-align:right;white-space:nowrap}
a{color:var(--accent)}
.pill{font-size:12px;padding:3px 10px;border-radius:999px;border:1px solid var(--line);color:var(--muted)}
.pill.ok{color:var(--c2);border-color:var(--c2)}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.muted{color:var(--muted);font-size:13px;margin:8px 0 0}
.seg{display:inline-flex;border:1px solid var(--line);border-radius:8px;overflow:hidden}
.seg button{border:0;border-radius:0}
.seg button.on{background:var(--accent);color:#fff}
.legend{display:flex;gap:12px;font-size:12px;color:var(--muted);margin:6px 0 0;flex-wrap:wrap}
.legend i{display:inline-block;width:14px;height:3px;vertical-align:middle;margin-right:4px}
</style></head><body>
<header>
  <h1>CADSE IMU logger</h1>
  <div class="row">
    <div class="seg" id="src">
      <button data-s="bno" class="on">BNO085</button><button data-s="ob">Onboard</button><button data-s="both">Both</button>
    </div>
    <span id="conn" class="pill">connecting</span>
  </div>
</header>
<main>
  <section class="card">
    <h2>Orientation</h2>
    <div class="vals" id="vals"></div>
    <canvas id="cube" height="230"></canvas>
    <p class="muted" id="info"></p>
  </section>
  <section class="card">
    <h2>Angles, last 10 s (deg)</h2>
    <canvas id="chAng" height="150"></canvas>
    <h2 style="margin-top:14px">Acceleration (m/s&sup2;)</h2>
    <canvas id="chAcc" height="150"></canvas>
    <h2 style="margin-top:14px">Rotation rate (deg/s)</h2>
    <canvas id="chGyr" height="150"></canvas>
    <div class="legend" id="legend"></div>
  </section>
  <section class="card">
    <h2>Logging to SD card</h2>
    <div class="row">
      <select id="rate"><option>10</option><option>25</option><option selected>50</option><option>100</option></select>
      <span class="muted" style="margin:0">Hz</span>
      <button id="logbtn" class="primary">Start logging</button>
    </div>
    <p class="muted" id="logstate">Both sensors are written to the same CSV file.</p>
    <div class="row" style="margin-top:12px">
      <button id="export">Export live buffer as CSV</button>
    </div>
    <p class="muted">The live buffer is what this page has received, up to the last 30 minutes, independent of SD logging.</p>
  </section>
  <section class="card">
    <h2>Log files</h2>
    <table id="files"><tr><td class="muted">Loading</td></tr></table>
    <p class="muted" id="sd"></p>
  </section>
</main>
<script>
const $=id=>document.getElementById(id);
const N=100;                       // 10 s at 10 Hz polling
const keys=['by','bp','br','bax','bay','baz','bgx','bgy','bgz','oy','op','or','oax','oay','oaz','ogx','ogy','ogz'];
const H={};keys.forEach(k=>H[k]=[]);
const live=[];const LIVE_MAX=18000;
let src='bno',last=null,logOn=false;
const R2D=57.29578;
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();

document.querySelectorAll('#src button').forEach(b=>b.onclick=()=>{
  src=b.dataset.s;document.querySelectorAll('#src button').forEach(x=>x.classList.toggle('on',x===b));
  if(last)render(last);
});

function push(k,v){const a=H[k];a.push(v);if(a.length>N)a.shift();}

async function poll(){
  try{
    const r=await fetch('/live',{cache:'no-store'});
    const d=await r.json();last=d;
    const b=d.bno,o=d.ob;
    push('by',b.ypr[0]);push('bp',b.ypr[1]);push('br',b.ypr[2]);
    push('bax',b.lin[0]);push('bay',b.lin[1]);push('baz',b.lin[2]);
    push('bgx',b.gyr[0]*R2D);push('bgy',b.gyr[1]*R2D);push('bgz',b.gyr[2]*R2D);
    push('oy',o.ypr[0]);push('op',o.ypr[1]);push('or',o.ypr[2]);
    push('oax',o.acc[0]);push('oay',o.acc[1]);push('oaz',o.acc[2]);
    push('ogx',o.gyr[0]*R2D);push('ogy',o.gyr[1]*R2D);push('ogz',o.gyr[2]*R2D);
    live.push([d.t,...b.q,...b.ypr,...b.lin,...b.gyr,...b.mag,b.acc,...o.acc,...o.gyr,...o.ypr]);
    if(live.length>LIVE_MAX)live.shift();
    render(d);
    $('conn').textContent='live';$('conn').className='pill ok';
  }catch(e){$('conn').textContent='offline';$('conn').className='pill';}
  setTimeout(poll,100);
}

function render(d){
  const b=d.bno,o=d.ob,f=v=>v.toFixed(1)+'°';
  let rows='<span></span><span style="text-align:right">yaw</span><span style="text-align:right">pitch</span><span style="text-align:right">roll</span>';
  if(src!=='ob')rows+=`<span>BNO085</span><b>${f(b.ypr[0])}</b><b>${f(b.ypr[1])}</b><b>${f(b.ypr[2])}</b>`;
  if(src!=='bno')rows+=`<span>Onboard</span><b>${f(o.ypr[0])}</b><b>${f(o.ypr[1])}</b><b>${f(o.ypr[2])}</b>`;
  $('vals').innerHTML=rows;
  const acc=['unreliable','low','medium','high'][b.acc]||'-';
  const parts=[];
  if(src!=='ob')parts.push(b.ok?`BNO085 ${b.hz} Hz, accuracy ${acc}`:'BNO085 not connected');
  if(src!=='bno')parts.push(o.ok?`onboard LSM6DS3 ${o.temp.toFixed(1)} °C, yaw drifts (no compass)`:'onboard IMU not found');
  parts.push(`battery ${d.bat}%`);
  $('info').textContent=parts.join(' · ');

  const showB=src!=='ob',showO=src!=='bno';
  drawCube(showB?b.q:null,showO?o.q:null);
  const sets=(bk,ok)=>[...(showB?bk.map((k,i)=>({d:H[k],c:i,dash:false})):[]),...(showO?ok.map((k,i)=>({d:H[k],c:i,dash:src==='both'})):[])];
  chart('chAng',sets(['by','bp','br'],['oy','op','or']));
  chart('chAcc',sets(['bax','bay','baz'],['oax','oay','oaz']));
  chart('chGyr',sets(['bgx','bgy','bgz'],['ogx','ogy','ogz']));
  let lg='<span><i style="background:var(--c1)"></i>yaw / x</span><span><i style="background:var(--c2)"></i>pitch / y</span><span><i style="background:var(--c3)"></i>roll / z</span>';
  if(src==='both')lg+='<span>solid = BNO085, dashed = onboard</span>';
  if(showB)lg+='<span>BNO085 acceleration is linear (gravity removed)</span>';
  $('legend').innerHTML=lg;

  logOn=d.log.on;
  $('logbtn').textContent=logOn?'Stop logging':'Start logging';
  $('logbtn').className=logOn?'rec':'primary';
  $('rate').disabled=logOn;
  $('logstate').textContent=!d.sd?'No SD card in the board.':logOn?`Recording ${d.log.file} at ${d.log.rate} Hz, ${d.log.lines} lines`:'Not recording. Both sensors are written to the same CSV file.';
}

function fit(cv){
  if(!cv.dataset.h)cv.dataset.h=cv.getAttribute('height');
  const r=window.devicePixelRatio||1,w=cv.clientWidth,h=+cv.dataset.h;
  if(cv.width!==Math.round(w*r)){cv.width=Math.round(w*r);cv.height=Math.round(h*r);cv.style.height=h+'px';}
  const g=cv.getContext('2d');g.setTransform(r,0,0,r,0,0);g.clearRect(0,0,w,h);return [g,w,h];
}

function chart(id,series){
  const [g,w,h]=fit($(id));const cols=[css('--c1'),css('--c2'),css('--c3')];
  let lo=Infinity,hi=-Infinity;
  series.forEach(s=>s.d.forEach(v=>{lo=Math.min(lo,v);hi=Math.max(hi,v);}));
  if(!isFinite(lo)){lo=-1;hi=1;}
  if(hi-lo<1){const m=(hi+lo)/2;lo=m-.5;hi=m+.5;}
  const pad=(hi-lo)*.1;lo-=pad;hi+=pad;
  const L=40,y=v=>h-4-(v-lo)/(hi-lo)*(h-8);
  g.strokeStyle=css('--line');g.lineWidth=1;g.fillStyle=css('--muted');g.font='11px system-ui';
  [lo+pad,(lo+hi)/2,hi-pad].forEach(v=>{g.beginPath();g.moveTo(L,y(v));g.lineTo(w,y(v));g.stroke();g.fillText(v.toFixed(1),0,y(v)+4);});
  series.forEach(s=>{
    if(s.d.length<2)return;
    g.strokeStyle=cols[s.c];g.lineWidth=1.6;g.setLineDash(s.dash?[4,3]:[]);g.beginPath();
    s.d.forEach((v,i)=>{const x=L+(w-L)*(i+N-s.d.length)/(N-1);i?g.lineTo(x,y(v)):g.moveTo(x,y(v));});
    g.stroke();
  });
  g.setLineDash([]);
}

function rot(q,v){
  const [w,x,y,z]=q;
  const tx=2*(y*v[2]-z*v[1]),ty=2*(z*v[0]-x*v[2]),tz=2*(x*v[1]-y*v[0]);
  return [v[0]+w*tx+(y*tz-z*ty),v[1]+w*ty+(z*tx-x*tz),v[2]+w*tz+(x*ty-y*tx)];
}

function drawCube(qb,qo){
  const [g,w,h]=fit($('cube'));
  const V=[];for(const sx of[-1,1])for(const sy of[-1,1])for(const sz of[-1,1])V.push([sx*1.5,sy*1,sz*.18]);
  const F=[[0,1,3,2],[4,5,7,6],[0,1,5,4],[2,3,7,6],[0,2,6,4],[1,3,7,5]];
  const tilt=.45,ct=Math.cos(tilt),st=Math.sin(tilt),S=Math.min(w,h)*.32;
  const proj=p=>{const y=p[1]*ct-p[2]*st,z=p[1]*st+p[2]*ct;const k=5/(5+y);return [w/2+p[0]*S*k,h/2-z*S*k,y];};
  const axes=(P,q)=>{
    [[2.2,0,0,'--c1','x'],[0,1.7,0,'--c2','y'],[0,0,1.2,'--c3','z']].forEach(a=>{
      const e=proj(rot(q,a)),o=proj([0,0,0]);
      g.strokeStyle=css(a[3]);g.lineWidth=2;g.beginPath();g.moveTo(o[0],o[1]);g.lineTo(e[0],e[1]);g.stroke();
      g.fillStyle=css(a[3]);g.font='12px system-ui';g.fillText(a[4],e[0]+3,e[1]-3);
    });
  };
  const box=(q,solid)=>{
    const P=V.map(v=>proj(rot(q,v)));
    const faces=F.map((f,i)=>({f,i,z:f.reduce((s,j)=>s+P[j][2],0)/4})).sort((a,b)=>b.z-a.z);
    faces.forEach(o=>{
      g.beginPath();o.f.forEach((j,k)=>k?g.lineTo(P[j][0],P[j][1]):g.moveTo(P[j][0],P[j][1]));g.closePath();
      if(solid){g.fillStyle=o.i===5?css('--accent'):css('--card');g.globalAlpha=o.i===5?.55:.9;g.fill();g.globalAlpha=1;}
      g.strokeStyle=solid?css('--text'):css('--muted');g.setLineDash(solid?[]:[4,3]);g.lineWidth=1;g.stroke();g.setLineDash([]);
    });
    if(solid)axes(P,q);
  };
  if(qo)box(qo,!qb);
  if(qb)box(qb,true);
  g.fillStyle=css('--muted');g.font='11px system-ui';
  g.fillText(qb&&qo?'solid = BNO085, dashed = onboard':'top face highlighted',6,h-6);
}

$('logbtn').onclick=async()=>{
  const url=logOn?'/log/stop':'/log/start?rate='+$('rate').value;
  const r=await fetch(url,{method:'POST'});
  if(!r.ok)alert(await r.text());
  setTimeout(loadFiles,300);
};

$('export').onclick=()=>{
  const head='t_ms,bno_qw,bno_qx,bno_qy,bno_qz,bno_yaw,bno_pitch,bno_roll,bno_lin_x,bno_lin_y,bno_lin_z,bno_gyr_x,bno_gyr_y,bno_gyr_z,bno_mag_x,bno_mag_y,bno_mag_z,bno_accuracy,ob_acc_x,ob_acc_y,ob_acc_z,ob_gyr_x,ob_gyr_y,ob_gyr_z,ob_yaw,ob_pitch,ob_roll';
  const csv=head+'\n'+live.map(r=>r.join(',')).join('\n');
  const a=document.createElement('a');
  a.href=URL.createObjectURL(new Blob([csv],{type:'text/csv'}));
  a.download='cadse_live_'+new Date().toISOString().slice(0,19).replace(/[:T]/g,'-')+'.csv';
  a.click();
};

async function loadFiles(){
  try{
    const d=await (await fetch('/files',{cache:'no-store'})).json();
    const t=$('files');
    if(!d.files.length){t.innerHTML='<tr><td class="muted">No log files yet</td></tr>';}
    else{
      d.files.sort((a,b)=>b.name.localeCompare(a.name));
      t.innerHTML=d.files.map(f=>`<tr><td>${f.name}</td><td class="r">${(f.size/1024).toFixed(1)} KB</td>
        <td class="r"><a href="/download?f=${encodeURIComponent(f.name)}" download>Download</a></td>
        <td class="r"><button data-f="${f.name}">Delete</button></td></tr>`).join('');
      t.querySelectorAll('button').forEach(b=>b.onclick=async()=>{
        if(!confirm('Delete '+b.dataset.f+'?'))return;
        const r=await fetch('/delete?f='+encodeURIComponent(b.dataset.f),{method:'POST'});
        if(!r.ok)alert(await r.text());
        loadFiles();
      });
    }
    $('sd').textContent=d.freeMB?d.freeMB+' MB free on the SD card':'';
  }catch(e){}
}

poll();loadFiles();setInterval(loadFiles,5000);
</script>
</body></html>)HTML";
