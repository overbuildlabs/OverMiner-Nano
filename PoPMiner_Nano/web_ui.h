// Embedded settings page served by OverMiner Nano's HTTP server.
// Styled with the OverBuild Labs tokens (docs/DESIGN.md in the site repo):
// near-black surfaces, emerald actions, Kaspa teal for live data.
// Auto-refreshes stats every 2s; saves config via POST /api/config.

#pragma once

// ----- Login page -----
// Submitted with x-www-form-urlencoded password field. The .ino sets the
// authToken cookie on success. ?err=1 in the URL flips the error message on.
static const char LOGIN_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OverMiner Nano - Login</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:#0C0C0F;color:#EDEDEF;margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
.box{width:100%;max-width:340px;background:#131316;border:1px solid #26262C;border-radius:6px;padding:24px}
h1{color:#EDEDEF;margin:0 0 4px;font-size:22px;font-weight:600}
.sub{color:#A3A3A3;margin:0 0 18px;font-size:13px}
label{display:block;font-size:11px;color:#8A8A93;margin:10px 0 4px;text-transform:uppercase;letter-spacing:1px;font-family:ui-monospace,monospace}
input[type=password]{width:100%;padding:10px;background:#0C0C0F;border:1px solid #686872;border-radius:4px;color:#EDEDEF;font-family:ui-monospace,monospace;font-size:14px}
input:focus{outline:2px solid #10B981;outline-offset:1px;border-color:#10B981}
button{width:100%;padding:11px;border:0;border-radius:4px;font-weight:600;cursor:pointer;background:#10B981;color:#0C0C0F;margin-top:14px;font-size:14px}
button:hover{background:#34D399}
.err{color:#F87171;font-size:12px;margin-top:10px;min-height:1em}
.hint{color:#8A8A93;font-size:11px;margin-top:14px;text-align:center}
</style>
</head>
<body>
<div class="box">
<h1>OverMiner Nano</h1>
<p class="sub">Enter password to access settings</p>
<form method="POST" action="/login">
<label>Password</label>
<input type="password" name="password" autofocus required>
<button type="submit">Sign In</button>
<div class="err" id="err"></div>
</form>
<p class="hint">Default password: kaspa123</p>
</div>
<script>
if (location.search.indexOf("err=1") >= 0) {
  document.getElementById("err").textContent = "Wrong password.";
}
</script>
</body>
</html>
)rawhtml";



static const char WEB_UI_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OverMiner Nano</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:#0C0C0F;color:#EDEDEF;margin:0;padding:16px;min-height:100vh}
.wrap{max-width:640px;margin:0 auto}
h1{color:#EDEDEF;margin:0;font-size:26px;font-weight:600}
h2{color:#A3A3A3;margin:0 0 12px;font-size:12px;text-transform:uppercase;letter-spacing:1px;font-family:ui-monospace,monospace;display:flex;align-items:center;gap:8px}
h2:before{content:"";width:3px;height:12px;border-radius:1px;background:#70C7BA}
.sub{color:#A3A3A3;margin:4px 0 20px;font-size:14px}
.card{background:#131316;border:1px solid #26262C;border-radius:6px;padding:16px;margin-bottom:14px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.stat .lbl{color:#8A8A93;font-size:11px;text-transform:uppercase;letter-spacing:1px;font-family:ui-monospace,monospace}
.stat .val{font-size:24px;font-weight:600;font-family:ui-monospace,monospace;margin-top:2px;font-variant-numeric:tabular-nums}
.t{color:#70C7BA}.y,.p,.r{color:#EDEDEF}
label{display:block;font-size:11px;color:#8A8A93;margin:10px 0 4px;text-transform:uppercase;letter-spacing:1px;font-family:ui-monospace,monospace}
input[type=text]{width:100%;padding:10px;background:#0C0C0F;border:1px solid #686872;border-radius:4px;color:#EDEDEF;font-family:ui-monospace,monospace;font-size:13px}
input:focus{outline:2px solid #10B981;outline-offset:1px;border-color:#10B981}
button{padding:11px 16px;border:0;border-radius:4px;font-weight:600;cursor:pointer;margin:6px 8px 0 0;font-size:13px}
.btn-go{background:#10B981;color:#0C0C0F}
.btn-go:hover{background:#34D399}
.btn-warn{background:transparent;color:#EDEDEF;border:1px solid #3A3A42}
.btn-warn:hover{border-color:#686872}
.btn-stop{background:transparent;color:#F87171;border:1px solid #F87171}
.pill{display:inline-block;padding:2px 8px;border-radius:2px;border:1px solid;font-size:11px;font-weight:600;letter-spacing:.5px;margin-left:6px;vertical-align:middle;font-family:ui-monospace,monospace}
.on{border-color:#10B981;color:#34D399}
.off{border-color:#3A3A42;color:#A3A3A3}
.muted{color:#8A8A93;font-size:12px;margin-top:8px}
.toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%);background:#1B1B20;border:1px solid #3A3A42;border-radius:4px;padding:10px 16px;font-size:13px;opacity:0;transition:opacity .2s;pointer-events:none}
.toast.show{opacity:1}
@media (prefers-reduced-motion:reduce){.toast{transition:none}}
</style>
</head>
<body>
<div class="wrap">
<h1>OverMiner Nano <span id="pill" class="pill off">IDLE</span></h1>
<p class="sub">Kaspa lottery miner &middot; <span id="ip">…</span> &middot; <span id="fw">…</span></p>

<div class="card">
<h2>Live Stats</h2>
<div class="grid">
<div class="stat"><div class="lbl">Hashrate</div><div class="val t" id="hashrate">— H/s</div></div>
<div class="stat"><div class="lbl">Shares</div><div class="val y" id="shares">0/0</div></div>
<div class="stat"><div class="lbl">Difficulty</div><div class="val p" id="diff">—</div></div>
<div class="stat"><div class="lbl">Total Hashes</div><div class="val r" id="hashes">0</div></div>
</div>
<p class="muted" id="poolStatus">…</p>
</div>

<div class="card">
<h2>Configuration</h2>
<form id="cfg" onsubmit="return saveConfig(event)">
<label>Pool URL</label>
<input type="text" id="pool" name="pool" placeholder="pool.overbuildlabs.com:5558">
<label>Worker Name</label>
<input type="text" id="worker" name="worker" placeholder="OverMinerNano">
<label>Kaspa Wallet</label>
<input type="text" id="wallet" name="wallet" placeholder="kaspa:...">
<div style="margin-top:14px"><button class="btn-go" type="submit">Save &amp; Restart</button></div>
</form>
</div>

<div class="card">
<h2>Actions</h2>
<button id="btnMine" class="btn-go" onclick="toggleMine()">Start Mining</button>
<button class="btn-warn" onclick="restart()">Restart</button>
<button class="btn-stop" onclick="factoryReset()">Factory Reset</button>
</div>

<div class="card">
<h2>Firmware Update</h2>
<p style="color:#A3A3A3;font-size:13px;margin:0 0 10px">Current: <span id="curFw" class="t" style="color:#70C7BA;font-weight:700">…</span> &middot; Compile a new build in Arduino IDE, export the .bin, then upload here.</p>
<input type="file" id="fwFile" accept=".bin,.bin.gz" style="color:#fff;font-size:13px">
<div style="margin-top:10px">
<button class="btn-warn" onclick="uploadFirmware()">Upload &amp; Update</button>
</div>
<div id="otaProgress" style="display:none;margin-top:14px">
<div style="background:#0C0C0F;border-radius:4px;height:8px;overflow:hidden;border:1px solid #131316">
<div id="otaBar" style="height:100%;background:#70C7BA;width:0%;transition:width .15s"></div>
</div>
<p id="otaText" style="color:#A3A3A3;font-size:12px;margin:6px 0 0"></p>
</div>
</div>

<div class="card">
<h2>Account</h2>
<details>
<summary style="cursor:pointer;color:#A3A3A3;font-size:13px;margin-bottom:10px">Change password</summary>
<label>Current password</label>
<input type="password" id="oldPwd">
<label>New password (4+ chars)</label>
<input type="password" id="newPwd">
<button class="btn-go" onclick="changePassword()" style="margin-top:10px">Update Password</button>
</details>
<button class="btn-warn" onclick="logout()" style="margin-top:14px">Sign Out</button>
</div>
</div>

<div id="toast" class="toast"></div>

<script>
function fmt(n){return n>=1e9?(n/1e9).toFixed(2)+"G":n>=1e6?(n/1e6).toFixed(1)+"M":n>=1e3?(n/1e3).toFixed(1)+"K":String(n)}
function toast(m){var t=document.getElementById("toast");t.textContent=m;t.className="toast show";setTimeout(function(){t.className="toast"},2400)}
async function refresh(){try{
  var r=await fetch("/api/stats"),d=await r.json();
  document.getElementById("ip").textContent=d.ip;
  document.getElementById("fw").textContent="FW "+d.fw;
  var cfw=document.getElementById("curFw");if(cfw)cfw.textContent=d.fw;
  var pill=document.getElementById("pill");
  pill.textContent=d.mining?"MINING":"IDLE";
  pill.className="pill "+(d.mining?"on":"off");
  var btn=document.getElementById("btnMine");
  btn.textContent=d.mining?"Stop Mining":"Start Mining";
  btn.className=d.mining?"btn-stop":"btn-go";
  document.getElementById("hashrate").textContent=d.hashrate>=1000?(d.hashrate/1000).toFixed(2)+" KH/s":Math.round(d.hashrate)+" H/s";
  document.getElementById("shares").textContent=d.accepted+"/"+d.submitted+(d.rejected?" ("+d.rejected+"r)":"");
  document.getElementById("diff").textContent=d.difficulty>=1?d.difficulty.toFixed(3):d.difficulty.toFixed(6);
  document.getElementById("hashes").textContent=fmt(d.total_hashes);
  document.getElementById("poolStatus").textContent=(d.pool_connected?"Pool connected":"Pool DISCONNECTED")+" • "+d.pool+" • jobs:"+d.jobs;
}catch(e){}}
async function loadConfig(){var r=await fetch("/api/config"),d=await r.json();document.getElementById("pool").value=d.pool;document.getElementById("worker").value=d.worker;document.getElementById("wallet").value=d.wallet}
async function saveConfig(e){
  e.preventDefault();
  var data=new URLSearchParams();
  data.set("pool",document.getElementById("pool").value.trim());
  data.set("worker",document.getElementById("worker").value.trim());
  data.set("wallet",document.getElementById("wallet").value.trim());
  var r=await fetch("/api/config",{method:"POST",body:data});
  if(r.ok){toast("Saved. Restarting…");setTimeout(function(){fetch("/api/restart",{method:"POST"})},800)}
  else if(r.status===401){location.href="/login"}
  else{
    try{var d=await r.json();toast(d.error||"Save failed")}
    catch(_){toast("Save failed (HTTP "+r.status+")")}
  }
  return false;
}
async function toggleMine(){await fetch("/api/mine",{method:"POST"});toast("Mining toggled");setTimeout(refresh,400)}
async function restart(){if(!confirm("Restart device?"))return;await fetch("/api/restart",{method:"POST"});toast("Restarting…")}
async function factoryReset(){if(!confirm("Factory reset wipes WiFi, wallet, pool, worker. Continue?"))return;await fetch("/api/factory_reset",{method:"POST"});toast("Reset. Rebooting to setup AP…")}
function uploadFirmware(){
  var f=document.getElementById("fwFile").files[0];
  if(!f){toast("Pick a .bin first");return}
  if(!confirm("Upload "+f.name+" ("+(f.size/1024).toFixed(0)+" KB)? Mining will stop and the device will reboot."))return;
  var fd=new FormData();fd.append("firmware",f,f.name);
  document.getElementById("otaProgress").style.display="block";
  var bar=document.getElementById("otaBar"),txt=document.getElementById("otaText");
  bar.style.width="0%";txt.textContent="Uploading…";
  var xhr=new XMLHttpRequest();
  xhr.open("POST","/api/ota");
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){var p=(e.loaded/e.total)*100;bar.style.width=p+"%";txt.textContent="Uploading "+Math.round(p)+"% ("+(e.loaded/1024).toFixed(0)+" / "+(e.total/1024).toFixed(0)+" KB)"}
  };
  xhr.onload=function(){
    if(xhr.status===200){bar.style.width="100%";txt.textContent="Update applied. Device rebooting - reload in ~10s.";setTimeout(function(){location.href="/login"},10000)}
    else if(xhr.status===401){location.href="/login"}
    else{txt.textContent="Failed (HTTP "+xhr.status+"): "+xhr.responseText}
  };
  xhr.onerror=function(){txt.textContent="Network error during upload (the device may have already rebooted)"};
  xhr.send(fd);
}
async function logout(){await fetch("/logout",{method:"POST"});location.href="/login"}
async function changePassword(){
  var oldPwd=document.getElementById("oldPwd").value;
  var newPwd=document.getElementById("newPwd").value;
  if(!oldPwd||!newPwd){toast("Fill both fields");return}
  var data=new URLSearchParams();data.set("old",oldPwd);data.set("new",newPwd);
  var r=await fetch("/api/change_password",{method:"POST",body:data});
  if(r.ok){toast("Password changed - signing out");setTimeout(function(){location.href="/login"},1200)}
  else{var d=await r.json();toast(d.error||"Change failed")}
}
// Redirect to /login on any 401 from auth-gated calls
async function safeFetch(url, opts){
  var r = await fetch(url, opts);
  if (r.status === 401) { location.href = "/login"; throw new Error("auth"); }
  return r;
}
loadConfig();refresh();setInterval(refresh,2000);
</script>
</body>
</html>
)rawhtml";
