#pragma once
// The config page served by the frame while the config switch is on.
const char PORTAL_HTML[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>E-Ink Frame setup</title>
<style>
:root{--bg:#f4f3ef;--card:#fff;--text:#1f2328;--muted:#5f6670;--line:#d9d7d0;--accent:#2f6f4f;--warn:#b3261e;--warnbg:#fdecea}
@media (prefers-color-scheme:dark){:root{--bg:#15171a;--card:#1f2226;--text:#e8e6e1;--muted:#a0a6ad;--line:#353a40;--accent:#6fbf8f;--warn:#f2a49d;--warnbg:#3a1f1d}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,sans-serif}
main{max-width:720px;margin:0 auto;padding:16px;display:grid;gap:14px}
h1{font-size:21px;margin:4px 0}h2{font-size:16px;margin:0 0 10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
.warn{background:var(--warnbg);border-color:var(--warn);color:var(--warn);font-weight:600}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}@media (max-width:520px){.grid{grid-template-columns:1fr}}
label{display:grid;gap:4px;font-size:14px;color:var(--muted)}
input,button{font:inherit;color:inherit;background:var(--card);border:1px solid var(--line);border-radius:8px;padding:7px 10px}
input[type=checkbox]{width:auto}.check{display:flex;gap:8px;align-items:center}
button{cursor:pointer}button.primary{background:var(--accent);color:#fff;border-color:var(--accent)}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:center}
.msg{min-height:1.3em;color:var(--muted);margin:8px 0 0}
table{border-collapse:collapse;width:100%}
.battery{display:flex;gap:12px;align-items:center;margin-bottom:10px;font-weight:600}
.meter{width:120px;height:18px;border:2px solid var(--muted);border-radius:4px;padding:2px;position:relative}
.meter::after{content:"";position:absolute;right:-6px;top:4px;width:4px;height:8px;background:var(--muted);border-radius:1px}
#meterfill{height:100%;width:0;background:var(--accent);border-radius:2px}
.battery.low #meterfill{background:var(--warn)}.battery.low{color:var(--warn)}td{padding:3px 0;vertical-align:top}td:first-child{color:var(--muted);width:42%}
</style></head><body><main>
<h1>E-Ink Frame setup</h1>
<div class="card warn">Config mode keeps the frame awake and drains the battery (about a day on a full charge). When you're done, turn the config switch off, or hold the button for 3 s (or press Restart). Without a switch it turns itself off after 30 minutes.</div>

<section class="card"><h2>Status</h2>
<div id="battery" class="battery"><div class="meter"><div id="meterfill"></div></div><span id="battext">Battery: reading…</span></div>
<p class="msg" id="battime" style="margin:-4px 0 8px"></p>
<table id="status"></table></section>

<section class="card"><h2>Show a picture now</h2>
<p class="msg" style="margin-top:-4px">Use the <b>.bin</b> button on any picture in the Home Assistant add-on to get the file. It must be exactly 960,000 bytes.</p>
<form id="pic" class="row"><input type="file" id="bin" accept=".bin,application/octet-stream"><button class="primary">Upload and show</button></form>
<div class="row" style="margin-top:10px">
<button type="button" data-act="fetch">Get next picture from server</button>
<button type="button" data-act="test">Colour test</button>
<button type="button" data-act="clear">Clear to white</button>
<button type="button" data-act="reboot">Restart</button></div>
<p class="msg" id="picmsg"></p></section>

<section class="card"><h2>Settings</h2>
<form id="settings">
<div class="grid">
<label>Wi-Fi network<input name="ssid" required></label>
<label>Wi-Fi password<input name="pass" type="password" placeholder="unchanged"></label>
<label>Backup Wi-Fi network (optional)<input name="ssid2"></label>
<label>Backup Wi-Fi password<input name="pass2" type="password" placeholder="unchanged"></label>
<label>Server address<input name="host" required></label>
<label>Server port<input name="port" type="number" min="1" max="65535" required></label>
<label>Frame token<input name="token" type="password" placeholder="unchanged"></label>
<label>Setup hotspot password (8+ chars)<input name="appass" type="password" placeholder="unchanged" minlength="8"></label>
</div>
<p class="check"><input type="checkbox" name="static" id="static"><label for="static" style="display:inline">Use a fixed IP address (faster wake-ups)</label></p>
<div class="grid">
<label>IP address<input name="ip"></label><label>Gateway<input name="gw"></label>
<label>Subnet mask<input name="mask"></label><label>DNS<input name="dns"></label>
</div>
<p class="check"><input type="checkbox" name="divider" id="divider"><label for="divider" style="display:inline">Battery reading fitted (XIAO: divider on D3; Feather: built in)</label></p>
<div class="row"><button class="primary">Save settings</button></div>
<p class="msg" id="setmsg">Wi-Fi and server changes take effect when you turn config mode off.</p>
</form></section>

<section class="card"><h2>Firmware update</h2>
<p class="msg" style="margin-top:-4px">Arduino IDE: Sketch &rarr; Export Compiled Binary, then upload <b>eink_frame.ino.bin</b>.</p>
<form id="fw" class="row"><input type="file" id="fwfile" accept=".bin"><button>Update firmware</button></form>
<p class="msg" id="fwmsg"></p></section>
</main>
<script>
const $=s=>document.querySelector(s);
const esc=v=>String(v).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
async function status(){
  const s=await (await fetch('/api/status')).json();
  const pct=s.battery_pct, low=pct>=0&&pct<=20;
  $('#battery').classList.toggle('low',low);
  $('#meterfill').style.width=(pct>=0?pct:0)+'%';
  $('#battext').textContent=pct<0?'Battery: unknown (no divider fitted)':
    `Battery ${pct}% · ${(s.battery_mv/1000).toFixed(2)} V`+(s.battery_mv>=4150?' · charging or full':'')+
    (low?' · low, charge it soon':'');
  $('#battime').textContent=pct<0?'':`At about 100 mA in config mode, a 3000 mAh battery at ${pct}% lasts roughly ${Math.max(1,Math.round(pct*0.3))} h.`;
  const cfg=s.cfg_trial>0?`v${s.cfg_version} (trying on next wake)`:`v${s.cfg_version}`+
    (s.cfg_failed>s.cfg_version?` · v${s.cfg_failed} failed: ${s.cfg_fail_reason}`:'');
  const rows=[['Firmware',s.fw+(s.fw_failed?` (update to ${s.fw_failed} failed: ${s.fw_error})`:'')],
    ['Settings from server',cfg],
    ['Home Wi-Fi',s.sta_connected?`${s.sta_ssid} · ${s.sta_ip} · ${s.rssi} dBm`:'not connected'],
    ['Setup hotspot',`${s.ap_ssid} · ${s.ap_ip}`],['Also reachable at','http://eink-frame.local'],
    ['Server',`${s.host}:${s.port}`],['Picture on panel',s.current_id||'—'],['Awake for',Math.round(s.uptime_s/60)+' min']];
  $('#status').innerHTML=rows.map(r=>`<tr><td>${r[0]}</td><td>${esc(r[1])}</td></tr>`).join('');
  return s;
}
async function loadSettings(){
  const s=await status(); const f=$('#settings');
  for(const k of ['ssid','ssid2','host','port','ip','gw','mask','dns']) f[k].value=s[k]??'';
  f.static.checked=s.static; f.divider.checked=s.divider;
}
async function post(url,body,msg){
  msg.textContent='Working…';
  try{const r=await fetch(url,{method:'POST',body});const t=await r.text();msg.textContent=t;return r.ok;}
  catch(e){msg.textContent='Failed: '+e.message;return false;}
}
$('#pic').onsubmit=async e=>{e.preventDefault();const f=$('#bin').files[0];
  if(!f){$('#picmsg').textContent='Choose a .bin file first.';return;}
  if(f.size!==960000){$('#picmsg').textContent=`That file is ${f.size} bytes; it must be 960,000.`;return;}
  const fd=new FormData();fd.append('frame',f,'frame.bin');
  $('#picmsg').textContent='Uploading… the panel takes about 20 s to refresh.';
  await post('/api/display',fd,$('#picmsg'));status();};
document.querySelectorAll('[data-act]').forEach(b=>b.onclick=async()=>{
  if(b.dataset.act==='reboot'&&!confirm('Restart the frame?'))return;
  await post('/api/'+b.dataset.act,null,$('#picmsg'));status();});
$('#settings').onsubmit=async e=>{e.preventDefault();
  const fd=new URLSearchParams(new FormData(e.target));
  fd.set('static',e.target.static.checked?'1':'0');fd.set('divider',e.target.divider.checked?'1':'0');
  if(await post('/api/settings',fd,$('#setmsg')))loadSettings();};
$('#fw').onsubmit=async e=>{e.preventDefault();const f=$('#fwfile').files[0];
  if(!f){$('#fwmsg').textContent='Choose a firmware .bin first.';return;}
  if(!confirm('Update the firmware? The frame restarts afterwards.'))return;
  const fd=new FormData();fd.append('firmware',f,f.name);await post('/api/firmware',fd,$('#fwmsg'));};
loadSettings();setInterval(status,15000);
</script></body></html>)HTML";
