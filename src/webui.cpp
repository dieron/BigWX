// Web settings page and JSON API.
//
//   GET  /                 settings page (single page, talks to the API below)
//   GET  /api/config       settings as JSON (?download=1 to save a backup file)
//   POST /api/config       change settings (partial JSON is fine), also used to restore a backup
//   GET  /api/status       firmware, WiFi, memory, last update, current values
//   POST /api/action?do=   refresh | reboot | wifi | reset
//   GET  /shot.bmp         screenshot: ?page=<id>&night=0|1&demo=1, or ?splash=1..4
//   POST /update           firmware update (.bin, multipart upload)
//
// If a password is set, everything asks for it (user name "admin", HTTP basic auth).

#include "webui.h"
#include "settings.h"
#include "weather.h"
#include "ui.h"
#include "version.h"
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>
#include <ArduinoJson.h>

static WebServer server(80);
volatile bool portalRequested = false;
static bool uploadDenied = false;

static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BigWX settings</title>
<style>
:root{--bg:#10141c;--card:#1b2230;--fg:#f2f4f8;--dim:#aab3c5;--acc:#5ccdff;--ok:#6ee787;--bad:#ff7a6b;--in:#0e131b;--bd:#3a4560}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:20px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:820px;margin:0 auto;padding:16px 16px 110px}
h1{font-size:30px;margin:8px 0 4px}
h2{font-size:24px;margin:0 0 10px}
h3{font-size:20px;margin:16px 0 4px;color:var(--acc)}
section{background:var(--card);border-radius:14px;padding:18px;margin:0 0 18px}
label{display:block;margin:12px 0 4px;color:var(--dim)}
label.inline{display:flex;align-items:center;gap:12px;color:var(--fg);margin:12px 0}
input,select,button{font:inherit;color:inherit}
input[type=text],input[type=number],input[type=password],select{width:100%;padding:10px 12px;border-radius:10px;border:2px solid var(--bd);background:var(--in)}
input:focus,select:focus,button:focus{outline:3px solid var(--acc);outline-offset:2px}
input[type=range]{width:100%;height:36px}
input[type=checkbox]{width:28px;height:28px;flex:none}
.row{display:flex;gap:12px;flex-wrap:wrap}.row>div{flex:1;min-width:200px}
.search{display:flex;gap:12px;align-items:center}.search input{flex:1;min-width:0}.search button{margin:0}
button{padding:12px 20px;border-radius:10px;border:0;background:var(--acc);color:#001018;font-weight:700;cursor:pointer;margin:8px 8px 0 0}
button.sec{background:#33405a;color:var(--fg)}
button.warn{background:#5a3333;color:var(--fg)}
button.small{padding:6px 14px;margin:0}
button:disabled{opacity:.3;cursor:default}
.note{color:var(--dim);font-size:17px}
#status{color:var(--dim);font-size:17px;margin-bottom:16px}
#results div{padding:10px 12px;border-radius:10px;background:var(--in);margin-top:6px;cursor:pointer}
#results div:hover{outline:2px solid var(--acc)}
.page{display:flex;align-items:center;gap:12px;padding:10px 12px;background:var(--in);border-radius:10px;margin-top:8px}
.page span{flex:1}
.page.off span{color:var(--dim);text-decoration:line-through}
#preview{width:100%;max-width:640px;aspect-ratio:320/170;image-rendering:pixelated;border-radius:8px;background:#000;display:block}
#bar{position:fixed;left:0;right:0;bottom:0;background:#0b0f16ee;border-top:2px solid var(--bd);padding:10px 16px;text-align:center}
#bar button{margin:0 6px;min-width:180px}
#msg{display:inline-block;margin-left:12px}
.ok{color:var(--ok)}.bad{color:var(--bad)}
progress{width:100%;height:24px}
footer{color:var(--dim);font-size:16px;margin-top:24px}
footer a{color:var(--acc)}
</style></head><body><main>
<h1>BigWX settings</h1>
<div id="status">Loading...</div>

<section><h2>Screen preview</h2>
<img id="preview" alt="Current screen of the display">
<div class="row" style="align-items:end">
<div><label for="pvpage">Page</label><select id="pvpage" onchange="shot()"></select></div>
<div><label for="pvmode">Colours</label><select id="pvmode" onchange="shot()"><option value="">As now</option><option value="0">Day</option><option value="1">Night</option></select></div>
<div style="flex:0"><button type="button" class="sec" onclick="shot()">Refresh</button></div>
</div>
<p class="note">The preview shows the saved settings. Save first to see a change.</p>
</section>

<section><h2>Location</h2>
<label for="q">Search for a place</label>
<div class="search"><input type="text" id="q" placeholder="City or town"><button type="button" class="sec" onclick="search()">Search</button></div>
<div id="results"></div>
<label for="lname">Name shown on the display</label><input type="text" id="lname" maxlength="28">
<div class="row"><div><label for="lat">Latitude</label><input type="number" step="any" id="lat"></div>
<div><label for="lon">Longitude</label><input type="number" step="any" id="lon"></div></div>
<p class="note">Leave latitude and longitude empty to find the location from your internet address.</p>
</section>

<section><h2>Units</h2>
<div class="row">
<div><label for="temp">Temperature</label><select id="temp"><option value="C">&deg;C</option><option value="F">&deg;F</option></select></div>
<div><label for="wind">Wind</label><select id="wind"><option value="kmh">km/h</option><option value="mph">mph</option><option value="ms">m/s</option><option value="kn">knots</option></select></div>
</div><div class="row">
<div><label for="pressure">Pressure</label><select id="pressure"><option value="hPa">hPa</option><option value="inHg">inHg</option><option value="mmHg">mmHg</option></select></div>
<div><label for="length">Waves</label><select id="length"><option value="m">metres</option><option value="ft">feet</option></select></div>
</div>
<label for="clock">Clock</label><select id="clock"><option value="24">24 hour</option><option value="12">12 hour (AM / PM)</option></select>
</section>

<section><h2>Pages</h2>
<p class="note">Tick the pages to show and use the arrows to change the order. The first page is the main page.
BOOT goes to the next page, KEY to the previous one.</p>
<div id="pages"></div>

<h3>Main page</h3>
<div class="row">
<div><label for="mstep">Forecast rows every</label><select id="mstep"><option value="1">1 hour</option><option value="2">2 hours</option><option value="3">3 hours</option><option value="4">4 hours</option><option value="6">6 hours</option></select></div>
<div><label for="mline2">Under the temperature</label><select id="mline2"><option value="condition">Condition (e.g. Partly cloudy)</option><option value="feels">Feels like temperature</option></select></div>
</div>
<h3>Next hours</h3>
<div class="row">
<div><label for="hspan">Time span</label><select id="hspan"><option value="12">12 hours</option><option value="24">24 hours</option><option value="36">36 hours</option><option value="48">48 hours</option></select></div>
<div><label class="inline" style="margin-top:44px"><input type="checkbox" id="hrain">Rain chance bars</label></div>
</div>
<h3>Next days</h3>
<div class="row">
<div><label for="dcount">Number of days</label><select id="dcount"><option value="3">3 (larger)</option><option value="4">4</option></select></div>
<div><label class="inline" style="margin-top:44px"><input type="checkbox" id="dtoday">Start with today</label></div>
</div>
<h3>Ocean</h3>
<label class="inline"><input type="checkbox" id="ocustom" onchange="oc()">Use a different place for the sea (a beach)</label>
<div id="ocoords" class="row"><div><label for="olat">Beach latitude</label><input type="number" step="any" id="olat"></div>
<div><label for="olon">Beach longitude</label><input type="number" step="any" id="olon"></div></div>
<label for="tide">Tides from</label><select id="tide" onchange="oc()">
<option value="model">Open-Meteo sea level model (worldwide, approximate)</option>
<option value="noaa">NOAA tide station (United States, exact)</option></select>
<div id="noaa"><label for="station">NOAA station id</label><input type="text" id="station" maxlength="12">
<button type="button" class="sec" onclick="nearestTide()">Find the nearest station</button>
<div id="tideinfo" class="note"></div></div>
</section>

<section><h2>Display</h2>
<label for="mode">Colours</label><select id="mode">
<option value="auto">Automatic: day colours by day, night colours after sunset</option>
<option value="day">Always day colours</option><option value="night">Always night colours</option></select>
<label for="palette">Night colour</label><select id="palette"><option value="amber">Amber</option><option value="red">Red (darkest)</option><option value="grey">Grey</option></select>
<label class="inline"><input type="checkbox" id="colours">Colour values by level in the day (off: plain white text)</label>
<label for="bday">Day brightness: <b id="vbday"></b></label><input type="range" min="10" max="255" id="bday" oninput="vbday.textContent=this.value">
<label for="bnight">Night brightness: <b id="vbnight"></b></label><input type="range" min="5" max="255" id="bnight" oninput="vbnight.textContent=this.value">
<label class="inline"><input type="checkbox" id="flip">Turn the screen upside down</label>
<div class="row">
<div><label for="cycle">Change page every (seconds, 0 = off)</label><input type="number" min="0" max="3600" id="cycle"></div>
<div><label for="back">Back to the main page after (seconds, 0 = never)</label><input type="number" min="0" max="3600" id="back"></div>
</div>
</section>

<section><h2>Device</h2>
<div class="row">
<div><label for="update">Update the forecast every (minutes)</label><input type="number" min="5" max="60" id="update"></div>
<div><label for="host">Network name</label><input type="text" id="host" maxlength="24"></div>
</div>
<p class="note">The network name gives the address http://<b id="hostv"></b>.local/ (after a restart).</p>
<label for="pw">Settings password (user name: admin)</label><input type="password" id="pw" autocomplete="new-password" placeholder="leave empty to keep the current one">
<label class="inline"><input type="checkbox" id="nopw">Remove the password</label>
<div>
<button type="button" class="sec" onclick="act('refresh')">Update weather now</button>
<button type="button" class="sec" onclick="if(confirm('Restart the display?'))act('reboot')">Restart</button>
<button type="button" class="sec" onclick="if(confirm('Open WiFi setup on the display? This page will stop working until it is connected again.'))act('wifi')">WiFi setup</button>
<button type="button" class="warn" onclick="if(confirm('Reset all settings to the defaults? WiFi stays connected.'))act('reset')">Reset settings</button>
</div>
</section>

<section><h2>Backup and firmware</h2>
<button type="button" class="sec" onclick="location='/api/config?download=1'">Download settings backup</button>
<label for="restore">Restore settings from a backup file</label><input type="file" id="restore" accept=".json,application/json" onchange="restore(this)">
<label for="fw">Firmware update (firmware.bin)</label><input type="file" id="fw" accept=".bin">
<button type="button" onclick="flash()">Install firmware</button>
<progress id="fwp" value="0" max="100" hidden></progress>
</section>

<footer>BigWX <span id="ver"></span>. Weather data by <a href="https://open-meteo.com/">Open-Meteo.com</a> (CC BY 4.0).
Tides: Open-Meteo or <a href="https://tidesandcurrents.noaa.gov/">NOAA CO-OPS</a>.</footer>
</main>
<div id="bar"><button type="button" onclick="save()">Save settings</button><span id="msg" role="status"></span></div>
<script>
const $=id=>document.getElementById(id);
let C={},P=[];
function msg(t,ok){$('msg').textContent=t;$('msg').className=ok?'ok':'bad';if(ok)setTimeout(()=>{if($('msg').textContent===t)$('msg').textContent=''},5000);}
async function api(u,o){const r=await fetch(u,o);if(!r.ok)throw new Error(await r.text()||r.status);return r.json();}
function fill(c){
 C=c;const L=c.location,U=c.units,D=c.display,O=c.ocean,V=c.device;
 $('lname').value=L.name;$('lat').value=L.located?L.lat:'';$('lon').value=L.located?L.lon:'';
 $('temp').value=U.temp;$('wind').value=U.wind;$('pressure').value=U.pressure;$('length').value=U.length;$('clock').value=U.clock24?'24':'12';
 P=c.pages.map(p=>({...p}));drawPages();
 $('mstep').value=c.main.step;$('mline2').value=c.main.line2;
 $('hspan').value=c.hours.span;$('hrain').checked=c.hours.rain;
 $('dcount').value=c.days.count;$('dtoday').checked=c.days.today;
 $('ocustom').checked=O.custom;$('olat').value=O.lat;$('olon').value=O.lon;$('tide').value=O.tide;$('station').value=O.station;oc();
 $('mode').value=D.mode;$('palette').value=D.palette;$('colours').checked=D.colours;$('flip').checked=D.flip;
 $('bday').value=D.day;$('bnight').value=D.night;vbday.textContent=D.day;vbnight.textContent=D.night;
 $('cycle').value=D.cycle;$('back').value=D.back;
 $('update').value=V.update;$('host').value=V.host;$('hostv').textContent=V.host;
 $('pw').value='';$('nopw').checked=false;$('pw').placeholder=V.hasPassword?'leave empty to keep the current one':'no password set';
 const s=$('pvpage'),cur=s.value;s.innerHTML='';
 for(const p of c.pages){const o=document.createElement('option');o.value=p.id;o.textContent=p.name;s.appendChild(o);}
 if(cur)s.value=cur;
}
function drawPages(){
 const box=$('pages');box.innerHTML='';
 P.forEach((p,i)=>{const d=document.createElement('div');d.className='page'+(p.on?'':' off');
  const cb=document.createElement('input');cb.type='checkbox';cb.checked=p.on;cb.id='pg_'+p.id;cb.onchange=()=>{p.on=cb.checked;drawPages();};
  const sp=document.createElement('span');sp.innerHTML='<label for="pg_'+p.id+'" style="display:inline;color:inherit;margin:0">'+(i+1)+'. '+p.name+'</label>';
  const up=document.createElement('button');up.type='button';up.className='sec small';up.textContent='↑';up.title='Move up';up.setAttribute('aria-label','Move '+p.name+' up');
  up.disabled=i===0;up.onclick=()=>{[P[i-1],P[i]]=[P[i],P[i-1]];drawPages();};
  const dn=document.createElement('button');dn.type='button';dn.className='sec small';dn.textContent='↓';dn.title='Move down';dn.setAttribute('aria-label','Move '+p.name+' down');
  dn.disabled=i===P.length-1;dn.onclick=()=>{[P[i+1],P[i]]=[P[i],P[i+1]];drawPages();};
  d.append(cb,sp,up,dn);box.appendChild(d);});
}
function oc(){$('ocoords').style.display=$('ocustom').checked?'':'none';$('noaa').style.display=$('tide').value==='noaa'?'':'none';}
function collect(){
 const num=id=>$(id).value===''?null:Number($(id).value);
 const c={location:{name:$('lname').value},
  units:{temp:$('temp').value,wind:$('wind').value,pressure:$('pressure').value,length:$('length').value,clock24:$('clock').value==='24'},
  pages:P.map(p=>({id:p.id,on:p.on})),
  main:{step:+$('mstep').value,line2:$('mline2').value},
  hours:{span:+$('hspan').value,rain:$('hrain').checked},
  days:{count:+$('dcount').value,today:$('dtoday').checked},
  ocean:{custom:$('ocustom').checked,lat:num('olat'),lon:num('olon'),tide:$('tide').value,station:$('station').value},
  display:{mode:$('mode').value,palette:$('palette').value,colours:$('colours').checked,flip:$('flip').checked,
   day:+$('bday').value,night:+$('bnight').value,cycle:+$('cycle').value,back:+$('back').value},
  device:{update:+$('update').value,host:$('host').value}};
 if($('lat').value!==''&&$('lon').value!==''){c.location.lat=+$('lat').value;c.location.lon=+$('lon').value;c.location.located=true;}
 else c.location.located=false;
 if($('nopw').checked)c.device.password='';else if($('pw').value)c.device.password=$('pw').value;
 return c;
}
async function save(){
 if(!P.some(p=>p.on)){msg('Show at least one page.',false);return;}
 try{fill(await api('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(collect())}));
  msg('Saved',true);setTimeout(shot,1500);status();}catch(e){msg('Not saved: '+e.message,false);}
}
async function act(a){try{await api('/api/action?do='+a,{method:'POST'});msg(a==='refresh'?'Updating...':'Done',true);
 if(a==='reset')load();if(a==='refresh')setTimeout(()=>{shot();status();},6000);}catch(e){msg(e.message,false);}}
function shot(){const m=$('pvmode').value;$('preview').src='/shot.bmp?page='+$('pvpage').value+(m?'&night='+m:'')+'&t='+Date.now();}
async function status(){try{const s=await api('/api/status');$('ver').textContent=s.version;
 $('status').textContent=[s.location||'No location yet',s.now?('now '+s.now):'',s.updated,'WiFi '+s.ssid+' ('+s.signal+')',s.ip].filter(Boolean).join(' · ');}catch(e){}}
async function load(){try{fill(await api('/api/config'));shot();status();}catch(e){$('status').textContent='Could not load settings: '+e.message;}}
$('q').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();search();}});
async function search(){
 const R=$('results');R.textContent='Searching...';
 try{const j=await (await fetch('https://geocoding-api.open-meteo.com/v1/search?count=8&language=en&format=json&name='+encodeURIComponent($('q').value))).json();
  R.textContent='';if(!j.results){R.textContent='Nothing found.';return;}
  for(const p of j.results){const d=document.createElement('div');d.tabIndex=0;
   d.textContent=[p.name,p.admin1,p.country].filter(Boolean).join(', ');
   d.onclick=d.onkeydown=e=>{if(e.type==='keydown'&&e.key!=='Enter')return;$('lname').value=p.name.slice(0,28);$('lat').value=p.latitude.toFixed(4);$('lon').value=p.longitude.toFixed(4);R.textContent='Selected: '+d.textContent+'. Press Save settings.';};
   R.appendChild(d);}
 }catch(e){R.textContent='Search needs internet access in this browser.';}
}
async function nearestTide(){
 const T=$('tideinfo');T.textContent='Loading NOAA stations...';
 const la=+($('ocustom').checked?$('olat').value:$('lat').value),lo=+($('ocustom').checked?$('olon').value:$('lon').value),k=Math.cos(la*Math.PI/180);
 try{const j=await (await fetch('https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=tidepredictions')).json();
  let best=null,bd=1e9;for(const s of j.stations){const d=Math.hypot(s.lat-la,(s.lng-lo)*k);if(d<bd){bd=d;best=s;}}
  if(best){$('station').value=best.id;T.textContent=best.name+' ('+best.id+'), about '+Math.round(bd*111)+' km away';}
 }catch(e){T.textContent='Could not load the station list.';}
}
async function restore(inp){
 const f=inp.files[0];if(!f)return;
 try{const c=JSON.parse(await f.text());if(!confirm('Replace the current settings with this backup?'))return;
  fill(await api('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(c)}));msg('Backup restored',true);shot();}
 catch(e){msg('Restore failed: '+e.message,false);}inp.value='';
}
function flash(){
 const f=$('fw').files[0];if(!f){msg('Choose a firmware.bin file first.',false);return;}
 if(!confirm('Install '+f.name+'? The display restarts when it is done.'))return;
 const x=new XMLHttpRequest(),fd=new FormData(),p=$('fwp');fd.append('firmware',f,f.name);p.hidden=false;
 x.upload.onprogress=e=>{if(e.lengthComputable)p.value=e.loaded/e.total*100;};
 x.onload=()=>{msg(x.status===200?'Installed. Restarting...':'Update failed: '+x.responseText,x.status===200);if(x.status===200)setTimeout(load,15000);};
 x.onerror=()=>msg('Update failed',false);x.open('POST','/update');x.send(fd);
}
load();setInterval(status,30000);
</script></body></html>)HTML";

// ---------------------------------------------------------------- helpers

static bool authed() {
  if (cfg.webPassword.isEmpty()) return true;
  if (server.authenticate("admin", cfg.webPassword.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "BigWX");
  return false;
}

static void sendJson(JsonDocument &doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void sendConfig() {
  JsonDocument doc;
  settingsToJson(doc, false);
  sendJson(doc);
}

static int pageFromArg() {
  String p = server.arg("page");
  for (int i = 0; i < PG_COUNT; i++)
    if (p == PAGE_KEYS[i]) return i;
  if (p.length() && isDigit(p[0])) return constrain(p.toInt(), 0, PG_COUNT - 1);
  PageId shown[PG_COUNT];
  uiPages(shown);
  return shown[0];
}

// ---------------------------------------------------------------- handlers

static void handleRoot() {
  if (!authed()) return;
  server.send_P(200, "text/html; charset=utf-8", PAGE);
}

static void handleGetConfig() {
  if (!authed()) return;
  if (server.hasArg("download")) {
    JsonDocument doc;
    settingsToJson(doc, false);
    doc.remove("pages");  // re-add without display names, keeps the file tidy
    JsonArray pages = doc["pages"].to<JsonArray>();
    for (int i = 0; i < PG_COUNT; i++) {
      JsonObject p = pages.add<JsonObject>();
      p["id"] = PAGE_KEYS[cfg.order[i]];
      p["on"] = cfg.shown[cfg.order[i]];
    }
    doc["device"].remove("hasPassword");
    String out;
    serializeJsonPretty(doc, out);
    server.sendHeader("Content-Disposition", "attachment; filename=\"bigwx-settings.json\"");
    server.send(200, "application/json", out);
    return;
  }
  sendConfig();
}

static void handlePostConfig() {
  if (!authed()) return;
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  bool locBefore = cfg.located;
  float latBefore = cfg.lat, lonBefore = cfg.lon;
  String err;
  if (!settingsFromJson(in.as<JsonVariantConst>(), err)) {
    server.send(400, "text/plain", err);
    return;
  }
  saveSettings();
  if (cfg.located != locBefore || cfg.lat != latBefore || cfg.lon != lonBefore) Serial.println("[web] location changed");
  weatherRefreshNow();
  sendConfig();
}

static void handleStatus() {
  if (!authed()) return;
  WxData d;
  weatherSnapshot(d);
  JsonDocument doc;
  doc["version"] = BIGWX_VERSION;
  doc["uptime"] = millis() / 1000;
  doc["ip"] = WiFi.localIP().toString();
  doc["ssid"] = WiFi.SSID();
  long rssi = WiFi.RSSI();
  doc["rssi"] = rssi;
  doc["signal"] = rssi > -60 ? "strong" : rssi > -72 ? "good" : rssi > -82 ? "weak" : "very weak";
  doc["heap"] = ESP.getFreeHeap();
  doc["psram"] = ESP.getFreePsram();
  doc["location"] = cfg.located ? cfg.name : String("");
  doc["located"] = cfg.located;
  if (d.ok) {
    char t[24];
    snprintf(t, sizeof(t), "%.0f\xC2\xB0%s", toTemp(d.now.temp), cfg.tempUnit == TEMP_F ? "F" : "C");
    doc["now"] = t;
    long mins = d.fetched > 1700000000 ? (time(nullptr) - d.fetched) / 60 : -1;
    doc["updated"] = mins < 0 ? "" : mins < 1 ? "updated just now" : ("updated " + String(mins) + " min ago");
  }
  if (d.err[0]) doc["error"] = d.err;
  sendJson(doc);
}

static void handleAction() {
  if (!authed()) return;
  String a = server.arg("do");
  if (a == "refresh") weatherRefreshNow();
  else if (a == "wifi") portalRequested = true;
  else if (a == "reset") { resetSettings(); weatherRefreshNow(); }
  else if (a != "reboot") { server.send(400, "text/plain", "Unknown action"); return; }
  server.send(200, "application/json", "{\"ok\":true}");
  if (a == "reboot") { delay(500); ESP.restart(); }
}

// Renders a page (or a start screen) and returns it as a 24-bit BMP.
static void handleShot() {
  if (!authed()) return;
  int night = server.hasArg("night") ? (server.arg("night") == "1") : -1;
  uiSetDemo(server.arg("demo") == "1");
  WxData d;
  weatherSnapshot(d);
  int splash = server.hasArg("splash") ? server.arg("splash").toInt() : 0;
  if (splash == 1) uiSplash("BigWX " BIGWX_VERSION, "Connecting to WiFi...", cfg.name);
  else if (splash == 2) uiSplash("BigWX " BIGWX_VERSION, "Connecting to MyWiFi", "Hold BOOT for WiFi setup");
  else if (splash == 3) uiSplash("BigWX " BIGWX_VERSION, "Connected", "192.168.1.50");
  else if (splash == 4) uiPortal("BigWX-Setup", "weather1");
  else uiDraw((PageId)pageFromArg(), d, night);
  uiSetDemo(false);

  const uint16_t *fb = uiFrame();
  if (!fb) { server.send(500, "text/plain", "no frame"); return; }
  const int w = 320, h = 170, row = w * 3;
  const uint32_t size = 54 + row * h;
  uint8_t hdr[54] = {'B', 'M'};
  auto put32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) hdr[o + i] = v >> (8 * i); };
  put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
  hdr[26] = 1; hdr[28] = 24; put32(34, row * h);
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(size);
  server.send(200, "image/bmp", "");
  server.sendContent((const char *)hdr, 54);
  static uint8_t line[320 * 3];
  for (int y = h - 1; y >= 0; y--) {
    for (int x = 0; x < w; x++) {
      uint16_t c = fb[y * w + x];
      c = (c >> 8) | (c << 8);
      line[x * 3 + 0] = (c & 0x1F) << 3;
      line[x * 3 + 1] = ((c >> 5) & 0x3F) << 2;
      line[x * 3 + 2] = (c >> 11) << 3;
    }
    server.sendContent((const char *)line, row);
  }
}

static void handleUpdateDone() {
  if (uploadDenied) { uploadDenied = false; server.requestAuthentication(BASIC_AUTH, "BigWX"); return; }
  bool ok = !Update.hasError();
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
  if (ok) {
    uiSplash("BigWX", "Update installed", "Restarting...");
    delay(800);
    ESP.restart();
  }
}

static void handleUpdateChunk() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadDenied = !cfg.webPassword.isEmpty() && !server.authenticate("admin", cfg.webPassword.c_str());
    if (uploadDenied) return;
    Serial.printf("[ota] %s\n", up.filename.c_str());
    uiSplash("BigWX", "Installing update...", "Do not unplug");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (uploadDenied) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

// ---------------------------------------------------------------- public

void webBegin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/shot.bmp", HTTP_GET, handleShot);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateChunk);
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void webLoop() { server.handleClient(); }
void webStop() { server.stop(); }
