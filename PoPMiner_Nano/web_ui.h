// Embedded settings page served by PoPMiner Nano's HTTP server.
// Styled to match proofofprints.com (deep navy + turquoise outlines).
// Auto-refreshes stats every 2s; saves config via POST /api/config.

#pragma once

// ----- WiFiManager captive portal theming -----
// Injected into every page WiFiManager serves while the device is in AP-setup
// mode. CSS recolors the default theme to match the rest of PoPMiner Nano
// (deep navy + turquoise outlines), and the JS rewrites the hardcoded button
// labels ("Configure WiFi" -> "Configure Device", "Erase" -> "Factory Reset",
// etc) so the captive portal feels branded instead of off-the-shelf.
static const char POPMINER_WM_HEAD[] PROGMEM = R"(
<style>
body{background:#070B1E !important;color:#fff !important;font-family:system-ui,-apple-system,sans-serif !important;margin:0;padding:16px}
.c,div{color:#fff}
.c{background:#1A1F3A !important;border:1px solid #49D9D3 !important;border-radius:8px !important;padding:18px !important;max-width:380px !important;margin:20px auto !important}
h1,h2,h3{color:#49D9D3 !important;letter-spacing:.5px;margin:8px 0}
h1{font-size:22px}
h3{font-size:14px;text-transform:uppercase;color:#9CA3AF !important}
input[type=text],input[type=password]{background:#070B1E !important;color:#fff !important;border:1px solid #1A1F3A !important;padding:10px !important;border-radius:4px !important;width:100% !important;box-sizing:border-box !important;margin:4px 0 !important;font-family:ui-monospace,monospace;font-size:13px}
input:focus{outline:none !important;border-color:#49D9D3 !important}
button,input[type=submit]{background:#49D9D3 !important;color:#070B1E !important;border:0 !important;padding:11px 16px !important;border-radius:6px !important;font-weight:700 !important;cursor:pointer !important;margin:6px 0 !important;font-size:14px;letter-spacing:.5px;width:100%}
.msg{background:#070B1E !important;color:#9CA3AF !important;border:1px solid #1A1F3A !important;border-radius:4px;padding:8px;font-size:13px}
.q{color:#6B7280 !important}
.l{color:#EAB308 !important}
hr{border:0;border-top:1px solid #1A1F3A}
small,em{color:#6B7280}
a{color:#49D9D3;text-decoration:none}
form{margin:0}
label{color:#9CA3AF;font-size:12px;text-transform:uppercase;letter-spacing:1px;display:block;margin:10px 0 4px}
</style>
<script>
function ppRebrand(){
  var labelMap={
    'Configure WiFi':'Configure Device',
    'Configure WiFi (No Scan)':'Manual Configure',
    'Info':'Device Info',
    'Setup':'Mining Setup',
    'Erase':'Factory Reset',
    'Restart':'Reboot',
    'Exit':'Done',
    'Update':'Update Firmware'
  };
  document.querySelectorAll('button,input[type=submit]').forEach(function(b){
    var t=(b.textContent||b.value||'').trim();
    if(labelMap[t]){
      if(b.textContent)b.textContent=labelMap[t];
      if(b.value)b.value=labelMap[t];
    }
  });
}
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',ppRebrand);
else ppRebrand();
</script>
)";

// ----- Login page -----
// Submitted with x-www-form-urlencoded password field. The .ino sets the
// authToken cookie on success. ?err=1 in the URL flips the error message on.
static const char LOGIN_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PoPMiner Nano - Login</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,sans-serif;background:#070B1E;color:#fff;margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
.box{width:100%;max-width:340px;background:#1A1F3A;border:1px solid #49D9D3;border-radius:8px;padding:24px}
h1{color:#49D9D3;margin:0 0 4px;font-size:22px;letter-spacing:.5px}
.sub{color:#6B7280;margin:0 0 18px;font-size:13px}
label{display:block;font-size:11px;color:#6B7280;margin:10px 0 4px;text-transform:uppercase;letter-spacing:1px}
input[type=password]{width:100%;padding:10px;background:#070B1E;border:1px solid #1A1F3A;border-radius:4px;color:#fff;font-family:ui-monospace,monospace;font-size:14px}
input:focus{outline:none;border-color:#49D9D3}
button{width:100%;padding:11px;border:0;border-radius:6px;font-weight:700;cursor:pointer;background:#49D9D3;color:#070B1E;margin-top:14px;font-size:14px;letter-spacing:.5px}
.err{color:#EF4444;font-size:12px;margin-top:10px;min-height:1em}
.hint{color:#6B7280;font-size:11px;margin-top:14px;text-align:center}
</style>
</head>
<body>
<div class="box">
<h1>PoPMiner Nano</h1>
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
<title>PoPMiner Nano</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,sans-serif;background:#070B1E;color:#fff;margin:0;padding:16px;min-height:100vh}
.wrap{max-width:640px;margin:0 auto}
h1{color:#49D9D3;margin:0;font-size:26px;letter-spacing:.5px}
h2{color:#49D9D3;margin:0 0 12px;font-size:16px;text-transform:uppercase;letter-spacing:1px}
.sub{color:#6B7280;margin:4px 0 20px;font-size:14px}
.card{background:#1A1F3A;border:1px solid #49D9D3;border-radius:8px;padding:16px;margin-bottom:14px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.stat .lbl{color:#6B7280;font-size:11px;text-transform:uppercase;letter-spacing:1px}
.stat .val{font-size:24px;font-weight:700;font-family:ui-monospace,monospace;margin-top:2px}
.t{color:#49D9D3}.y{color:#EAB308}.p{color:#A887E0}.r{color:#EF4444}
label{display:block;font-size:11px;color:#6B7280;margin:10px 0 4px;text-transform:uppercase;letter-spacing:1px}
input[type=text]{width:100%;padding:10px;background:#070B1E;border:1px solid #1A1F3A;border-radius:4px;color:#fff;font-family:ui-monospace,monospace;font-size:13px}
input:focus{outline:none;border-color:#49D9D3}
button{padding:11px 16px;border:0;border-radius:6px;font-weight:700;cursor:pointer;margin:6px 8px 0 0;font-size:13px;letter-spacing:.5px}
.btn-go{background:#49D9D3;color:#070B1E}
.btn-warn{background:#EAB308;color:#070B1E}
.btn-stop{background:#EF4444;color:#fff}
.pill{display:inline-block;padding:3px 10px;border-radius:11px;font-size:11px;font-weight:700;letter-spacing:.5px;margin-left:6px;vertical-align:middle}
.on{background:#22C55E;color:#070B1E}
.off{background:#374151;color:#9CA3AF}
.muted{color:#6B7280;font-size:12px;margin-top:8px}
.toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%);background:#1A1F3A;border:1px solid #49D9D3;border-radius:6px;padding:10px 16px;font-size:13px;opacity:0;transition:opacity .3s;pointer-events:none}
.toast.show{opacity:1}
</style>
</head>
<body>
<div class="wrap">
<h1>PoPMiner Nano <span id="pill" class="pill off">IDLE</span></h1>
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
<input type="text" id="pool" name="pool" placeholder="pool.proofofprints.com:5558">
<label>Worker Name</label>
<input type="text" id="worker" name="worker" placeholder="PoPMinerNano">
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
<p style="color:#9CA3AF;font-size:13px;margin:0 0 10px">Current: <span id="curFw" class="t" style="color:#49D9D3;font-weight:700">…</span> &middot; Compile a new build in Arduino IDE, export the .bin, then upload here.</p>
<input type="file" id="fwFile" accept=".bin,.bin.gz" style="color:#fff;font-size:13px">
<div style="margin-top:10px">
<button class="btn-warn" onclick="uploadFirmware()">Upload &amp; Update</button>
</div>
<div id="otaProgress" style="display:none;margin-top:14px">
<div style="background:#070B1E;border-radius:4px;height:8px;overflow:hidden;border:1px solid #1A1F3A">
<div id="otaBar" style="height:100%;background:#49D9D3;width:0%;transition:width .15s"></div>
</div>
<p id="otaText" style="color:#9CA3AF;font-size:12px;margin:6px 0 0"></p>
</div>
</div>

<div class="card">
<h2>Account</h2>
<details>
<summary style="cursor:pointer;color:#9CA3AF;font-size:13px;margin-bottom:10px">Change password</summary>
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
// Helper: clear our auth cookie + bounce to /login. Used after any action
// that reboots the device, since session tokens are RAM-only and don't
// survive a restart.
function ppLogoutAndLogin(delayMs){
  document.cookie="authToken=; Path=/; Max-Age=0; SameSite=Lax";
  setTimeout(function(){location.href="/login"},delayMs||10000);
}

async function saveConfig(e){
  e.preventDefault();
  var data=new URLSearchParams();
  data.set("pool",document.getElementById("pool").value.trim());
  data.set("worker",document.getElementById("worker").value.trim());
  data.set("wallet",document.getElementById("wallet").value.trim());
  var r=await fetch("/api/config",{method:"POST",body:data});
  if(r.ok){
    toast("Saved. Rebooting and signing you out…");
    setTimeout(function(){fetch("/api/restart",{method:"POST"})},800);
    ppLogoutAndLogin(10000);
  }
  else if(r.status===401){location.href="/login"}
  else{
    try{var d=await r.json();toast(d.error||"Save failed")}
    catch(_){toast("Save failed (HTTP "+r.status+")")}
  }
  return false;
}
async function toggleMine(){await fetch("/api/mine",{method:"POST"});toast("Mining toggled");setTimeout(refresh,400)}
async function restart(){
  if(!confirm("Restart device? You'll be signed out and the device will be unreachable for ~10 seconds."))return;
  await fetch("/api/restart",{method:"POST"});
  toast("Rebooting and signing you out…");
  ppLogoutAndLogin(10000);
}
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
