// Embedded settings page served by PoPMiner Nano's HTTP server.
// Styled to match proofofprints.com (deep navy + turquoise outlines).
// Auto-refreshes stats every 2s; saves config via POST /api/config.

#pragma once

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
<button class="btn-warn" onclick="toggleMine()">Toggle Mining</button>
<button class="btn-warn" onclick="restart()">Restart</button>
<button class="btn-stop" onclick="factoryReset()">Factory Reset</button>
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
  var pill=document.getElementById("pill");
  pill.textContent=d.mining?"MINING":"IDLE";
  pill.className="pill "+(d.mining?"on":"off");
  document.getElementById("hashrate").textContent=d.hashrate>=1000?(d.hashrate/1000).toFixed(2)+" KH/s":Math.round(d.hashrate)+" H/s";
  document.getElementById("shares").textContent=d.accepted+"/"+d.submitted+(d.rejected?" ("+d.rejected+"r)":"");
  document.getElementById("diff").textContent=d.difficulty>=1?d.difficulty.toFixed(3):d.difficulty.toFixed(6);
  document.getElementById("hashes").textContent=fmt(d.total_hashes);
  document.getElementById("poolStatus").textContent=(d.pool_connected?"Pool connected":"Pool DISCONNECTED")+" • "+d.pool+" • jobs:"+d.jobs;
}catch(e){}}
async function loadConfig(){var r=await fetch("/api/config"),d=await r.json();document.getElementById("pool").value=d.pool;document.getElementById("worker").value=d.worker;document.getElementById("wallet").value=d.wallet}
async function saveConfig(e){e.preventDefault();var data=new URLSearchParams();data.set("pool",document.getElementById("pool").value);data.set("worker",document.getElementById("worker").value);data.set("wallet",document.getElementById("wallet").value);var r=await fetch("/api/config",{method:"POST",body:data});if(r.ok){toast("Saved. Restarting…");setTimeout(function(){fetch("/api/restart",{method:"POST"})},800)}else toast("Save failed");return false}
async function toggleMine(){await fetch("/api/mine",{method:"POST"});toast("Mining toggled");setTimeout(refresh,400)}
async function restart(){if(!confirm("Restart device?"))return;await fetch("/api/restart",{method:"POST"});toast("Restarting…")}
async function factoryReset(){if(!confirm("Factory reset wipes WiFi, wallet, pool, worker. Continue?"))return;await fetch("/api/factory_reset",{method:"POST"});toast("Reset. Rebooting to setup AP…")}
loadConfig();refresh();setInterval(refresh,2000);
</script>
</body>
</html>
)rawhtml";
