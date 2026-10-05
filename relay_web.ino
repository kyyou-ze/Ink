#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>

// ===== PENGATURAN =====
const char* AP_SSID  = "Relay-Wemos";
const char* AP_PASS  = "12345678";
const char* WEB_USER = "admin";
const char* WEB_PASS = "123";           
#define NUM_RELAY 4  // Diubah menjadi 4 agar lebih seru                
const uint8_t PINS[4] = {D1, D2, D5, D6};
// ======================

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

struct Cfg {
  uint8_t  magic;
  uint8_t  restore;
  uint8_t  state[4];
  char     name[4][16];
  uint8_t  se[4];
  uint16_t onMin[4];
  uint16_t offMin[4];
  uint16_t pulse[4];
} cfg;

bool rs[4];
bool timerOn[4];
unsigned long offAt[4];
bool pulseOn[4];
unsigned long pulseUntil[4];

bool synced = false;
uint32_t baseSec = 0;
unsigned long baseMs = 0;
int lastMinute = -1;

void saveCfg() { EEPROM.put(0, cfg); EEPROM.commit(); }

void defaultCfg() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = 0xA5;
  cfg.restore = 1;
  for (int i = 0; i < 4; i++) {
    snprintf(cfg.name[i], 16, "Relay %d", i + 1);
    cfg.onMin[i] = 18 * 60;
    cfg.offMin[i] = 22 * 60;
  }
  saveCfg();
}

void applyRelay(int i) {
  if (rs[i]) {
    digitalWrite(PINS[i], LOW);
    pinMode(PINS[i], OUTPUT);
  } else {
    pinMode(PINS[i], INPUT);
  }
}

void setRelay(int i, bool on) {
  rs[i] = on;
  applyRelay(i);
  if (!on) { timerOn[i] = false; pulseOn[i] = false; }
  if (on && cfg.pulse[i] > 0) {
    pulseOn[i] = true;
    pulseUntil[i] = millis() + cfg.pulse[i];
  } else {
    cfg.state[i] = on;
    saveCfg();
  }
}

int secOfDay() { return (baseSec + (millis() - baseMs) / 1000) % 86400; }
String hhmm(int m) { char b[6]; snprintf(b, 6, "%02d:%02d", m / 60, m % 60); return String(b); }

bool auth() {
  if (strlen(WEB_PASS) == 0) return true;
  if (!server.authenticate(WEB_USER, WEB_PASS)) { server.requestAuthentication(); return false; }
  return true;
}

int chArg() { int c = server.arg("ch").toInt(); return (c < 0 || c >= NUM_RELAY) ? 0 : c; }

void sendStatus() {
  String j = "{\"n\":" + String(NUM_RELAY);
  j += ",\"up\":" + String(millis() / 1000);
  j += ",\"synced\":" + String(synced ? 1 : 0);
  j += ",\"time\":\"" + (synced ? hhmm(secOfDay() / 60) : String("--:--")) + "\"";
  j += ",\"restore\":" + String(cfg.restore);
  j += ",\"ch\":[";
  for (int i = 0; i < NUM_RELAY; i++) {
    if (i) j += ",";
    long left = timerOn[i] ? (long)(offAt[i] - millis()) / 1000 : 0;
    if (left < 0) left = 0;
    String safeName = String(cfg.name[i]); safeName.replace("\\",""); safeName.replace("\"","");
    j += "{\"name\":\"" + safeName + "\"";
    j += ",\"on\":" + String(rs[i] ? 1 : 0);
    j += ",\"left\":" + String(left);
    j += ",\"se\":" + String(cfg.se[i]);
    j += ",\"ont\":\"" + hhmm(cfg.onMin[i]) + "\"";
    j += ",\"offt\":\"" + hhmm(cfg.offMin[i]) + "\"";
    j += ",\"pulse\":" + String(cfg.pulse[i]) + "}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

const char PAGE[] PROGMEM = R"=====(<!DOCTYPE html><html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Smart Relay AI</title>
<style>
:root{--bg:#0f1115;--card:#1a1d24;--tx:#e8eaed;--mu:#8b93a1;--ac:#3ddc84;--bd:#2a2f3a;--rd:#ff5252}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--tx);padding:14px;max-width:560px;margin:auto}
h1{font-size:20px;margin:6px 0}.bar{display:flex;justify-content:space-between;color:var(--mu);font-size:13px;margin-bottom:12px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:14px;padding:14px;margin-bottom:12px}
.row{display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;margin-top:10px}
.name{font-weight:600;font-size:17px}.st{font-size:13px;color:var(--mu)}
.sw{position:relative;width:60px;height:32px;background:#3a3f4b;border-radius:20px;cursor:pointer;flex:none;transition:.2s}
.sw:after{content:"";position:absolute;top:3px;left:3px;width:26px;height:26px;background:#fff;border-radius:50%;transition:.2s}
.sw.on{background:var(--ac)}.sw.on:after{left:31px}
button{background:#2a2f3a;color:var(--tx);border:0;border-radius:8px;padding:8px 12px;font-size:14px;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:6px}
button:active{opacity:.7}
button.p{background:var(--ac);color:#06210f;font-weight:600}
button.mic{background:#2a2f3a;font-size:18px;width:40px;height:40px;border-radius:50%;transition:0.3s}
button.mic.rec{background:var(--rd);color:#fff;box-shadow:0 0 10px var(--rd)}
input[type=time],input[type=number],input[type=text],input[type=password]{background:#0f1115;color:var(--tx);border:1px solid var(--bd);border-radius:8px;padding:7px;font-size:14px}
input[type=number]{width:80px}input[type=text],input[type=password]{width:140px;flex:1}.lb{font-size:13px;color:var(--mu)}
a{color:var(--ac)}
.chat{height:220px;overflow-y:auto;background:#0f1115;border:1px solid var(--bd);border-radius:10px;padding:8px;font-size:14px;margin-top:10px;display:flex;flex-direction:column;gap:8px}
.msg{padding:8px 12px;border-radius:14px;max-width:85%;width:fit-content;white-space:pre-line;line-height:1.4}
.msg.ai{background:#2a2f3a;border-bottom-left-radius:4px}
.msg.me{background:var(--ac);color:#06210f;margin-left:auto;border-bottom-right-radius:4px}
.chips{overflow-x:auto;flex-wrap:nowrap}.chips button{font-size:12px;padding:5px 8px;white-space:nowrap}
#ci{flex:1;width:auto;min-width:0;height:40px}.aihint{font-size:12px;color:var(--mu);margin-top:6px}
</style></head><body>
<h1>Smart Relay AI</h1>
<div class="bar"><span id="clk">Jam: --:--</span><span id="upt">Uptime: -</span></div>

<div class="card">
  <div class="name" style="font-size:15px">Pengaturan Gemini AI</div>
  <div class="aihint">API Key disimpan lokal. Kosongkan jika belum punya.</div>
  <div class="row">
    <input type="password" id="gKey" placeholder="Paste Gemini API Key...">
    <button onclick="saveKey()">Simpan</button>
  </div>
</div>

<div class="card" style="border-color:var(--ac)">
<div class="name" style="font-size:15px;color:var(--ac)">Asisten Suara Gemini</div>
<div class="chat" id="chatlog"></div>
<div class="row" style="flex-wrap:nowrap">
<input type="text" id="ci" placeholder="Ketik atau bicara..." onkeydown="if(event.key==='Enter')sendAI()">
<button id="btnMic" class="mic" onclick="toggleMic()">🎤</button>
<button class="p" onclick="sendAI()" style="height:40px">Kirim</button>
</div>
<div class="row chips">
<button onclick="quickAI('Nyalakan semua lampu')">Nyalakan Semua</button>
<button onclick="quickAI('Matikan lampu teras 15 menit lagi')">Timer 15m</button>
<button onclick="quickAI('Buat jadwal nyala jam 18:00 dan mati jam 05:00')">Jadwal Malam</button>
</div>
</div>

<div class="row" style="margin:0 0 12px"><button class="p" onclick="setAll(1)">Semua ON</button><button onclick="setAll(0)">Semua OFF</button></div>
<div id="cards"></div>

<div class="card"><label class="row" style="margin:0"><span>Ingat status saat restart</span><input type="checkbox" id="rest" onchange="api('restore?v='+(this.checked?1:0))"></label>
<div class="row"><a href="/update">Update firmware (OTA)</a></div></div>

<script>
const $=id=>document.getElementById(id);let built=false;

// Simpan API Key
function saveKey(){localStorage.setItem('gAPI',$('gKey').value);alert('API Key tersimpan!');}
if(localStorage.getItem('gAPI'))$('gKey').value=localStorage.getItem('gAPI');

async function api(p){try{const r=await fetch('/api/'+p);return await r.json()}catch(e){return null}}
function fmt(s){const m=Math.floor(s/60);return m+':'+String(s%60).padStart(2,'0')}
function fmtUp(s){const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h+'j '+m+'m'}

function build(d){let h='';for(let i=0;i<d.n;i++){h+=`<div class="card">
<div class="row" style="margin:0"><div><div class="name" id="nm${i}"></div><div class="st" id="st${i}"></div></div><div class="sw" id="sw${i}" onclick="tg(${i})"></div></div>
<div class="row"><span class="lb">Timer</span><span><button onclick="tm(${i},15)">15m</button> <button onclick="tm(${i},60)">60m</button> <button onclick="tm(${i},0)">Batal</button></span></div>
<div class="row"><span class="lb">Jadwal harian</span><label><input type="checkbox" id="se${i}"> aktif</label></div>
<div class="row"><input type="time" id="on${i}"><input type="time" id="of${i}"><button onclick="sc(${i})">Simpan</button></div>
<div class="row"><span class="lb">Nama</span><span><input type="text" id="rn${i}" maxlength="15"> <button onclick="rn(${i})">Simpan</button></span></div>
</div>`}$('cards').innerHTML=h}

function fill(d){d.ch.forEach((c,i)=>{$('on'+i).value=c.ont;$('of'+i).value=c.offt;$('se'+i).checked=!!c.se;$('rn'+i).value=c.name})}
function upd(d){$('clk').textContent='Jam: '+d.time+(d.synced?'':' (belum sinkron)');$('upt').textContent='Uptime: '+fmtUp(d.up);$('rest').checked=!!d.restore;
d.ch.forEach((c,i)=>{$('nm'+i).textContent=c.name;$('sw'+i).className='sw'+(c.on?' on':'');$('st'+i).textContent=(c.on?'ON':'OFF')+(c.left>0?' — mati dlm '+fmt(c.left):'')})}

async function load(){const d=await api('status');if(!d)return;if(!built){build(d);fill(d);built=true;const n=new Date();api('time?s='+(n.getHours()*3600+n.getMinutes()*60+n.getSeconds()))}upd(d)}
async function tg(i){await api('toggle?ch='+i);load()}
async function setAll(s){await api('all?s='+s);load()}
async function tm(i,m){await api('timer?ch='+i+'&min='+(m||0));load()}
async function sc(i){await api(`sched?ch=${i}&en=${$('se'+i).checked?1:0}&on=${$('on'+i).value}&off=${$('of'+i).value}`);}
async function rn(i){await api(`name?ch=${i}&n=${encodeURIComponent($('rn'+i).value)}`);load()}

// ===== VOICE & CHAT ENGINE =====
function aiSay(t, me=false){
  const l=$('chatlog'); const d=document.createElement('div');
  d.className='msg '+(me?'me':'ai'); d.textContent=t; l.appendChild(d);
  l.scrollTop=l.scrollHeight;
  return d; // Return element for replacing text later (e.g. "Berpikir...")
}

function speak(txt) {
  if ('speechSynthesis' in window) {
    const utterance = new SpeechSynthesisUtterance(txt);
    utterance.lang = 'id-ID';
    utterance.rate = 1.0;
    speechSynthesis.speak(utterance);
  }
}

// Web Speech API (Microphone)
const SpeechRec = window.SpeechRecognition || window.webkitSpeechRecognition;
let recognition = null;
let isRecording = false;

if (SpeechRec) {
  recognition = new SpeechRec();
  recognition.lang = 'id-ID';
  recognition.interimResults = false;
  
  recognition.onstart = () => {
    isRecording = true;
    $('btnMic').classList.add('rec');$('ci').placeholder = "Mendengarkan...";
  };
  
  recognition.onresult = (e) => {
    const transcript = e.results[0][0].transcript;
    $('ci').value = transcript;
    sendAI();
  };
  
  recognition.onerror = (e) => {
    console.error(e);
    $('ci').placeholder = "Gagal mendengar, coba lagi.";
  };
  
  recognition.onend = () => {
    isRecording = false;
    $('btnMic').classList.remove('rec');$('ci').placeholder = "Ketik atau bicara...";
  };
}

function toggleMic() {
  if (!recognition) {
    alert("Browser Anda tidak mendukung fitur mikrofon (Gunakan Chrome/Safari versi terbaru, atau pastikan menggunakan HTTPS).");
    return;
  }
  if (isRecording) {
    recognition.stop();
  } else {
    recognition.start();
  }
}

function quickAI(t){$('ci').value=t;sendAI()}

async function sendAI(){
  const v=$('ci').value.trim(); if(!v)return;
  $('ci').value=''; aiSay(v, true); await aiAsk(v);
}

// ===== INTEGRASI GEMINI API =====
async function aiAsk(raw){
  const apiKey = localStorage.getItem('gAPI');
  if(!apiKey){ aiSay('Mohon masukkan Gemini API Key terlebih dahulu.'); return; }
  
  const d=await api('status');
  if(!d){aiSay('Wemos tidak terhubung.');return}
  
  const loadingMsg = aiSay('Berpikir...');

  const prompt = `Anda adalah asisten smart home bernama "Gemini". Jawab singkat, ramah, dan dalam bahasa Indonesia.
Kondisi saat ini: Waktu server ${d.time}, total ${d.n} relay. Detail relay: ${JSON.stringify(d.ch)}
Perintah user: "${raw}"

Hasilkan JSON Array dari daftar objek tindakan:
- "act": "set" (nyala/mati) ATAU "timer" (delay) ATAU "sched" (jadwal) ATAU "msg" (pesan suara/teks untuk membalas user).
- "ch": index relay (0 sampai ${d.n - 1}). Jika semua, buat object terpisah per index.
- "val": (1 ON, 0 OFF, atau menit timer).
- "on": "HH:MM", "off": "HH:MM" (untuk jadwal).
- "text": (jika act="msg", ini wajib diisi sebagai jawaban yang akan DIUCAPKAN oleh asisten, gunakan gaya bicara natural).

Contoh: 
[{"act":"msg","text":"Baik, lampu teras sudah saya nyalakan."}, {"act":"set","ch":0,"val":1}]

Hanya balas dengan raw JSON Array, tanpa markdown.`;

  try {
    const res = await fetch(`https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=${apiKey}`, {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({
        contents: [{parts: [{text: prompt}]}],
        generationConfig: { responseMimeType: "application/json", temperature: 0.3 }
      })
    });
    
    const resData = await res.json();
    if(resData.error) throw new Error(resData.error.message);
    
    const jsonText = resData.candidates[0].content.parts[0].text;
    const cmds = JSON.parse(jsonText);
    
    // Hapus pesan loading
    loadingMsg.remove();
    
    let hasAction = false;
    for(let c of cmds){
      if(c.act === 'set') { await api(`set?ch=${c.ch}&s=${c.val}`); hasAction = true; }
      else if(c.act === 'timer') { await api(`timer?ch=${c.ch}&min=${c.val}`); hasAction = true; }
      else if(c.act === 'sched') { await api(`sched?ch=${c.ch}&en=1&on=${c.on}&off=${c.off}`); hasAction = true; }
      else if(c.act === 'msg') { 
        aiSay(c.text);
        speak(c.text); // Mengucapkan jawaban lewat speaker
      }
    }
    
    if(hasAction) load(); // Refresh UI jika ada relay yang berubah status
    
  } catch(e) {
    loadingMsg.remove();
    aiSay("Ups, terjadi kesalahan koneksi ke AI.");
    console.error(e);
  }
}

aiSay('Halo! Saya asisten pintar rumahmu. Coba tekan tombol mikrofon dan bicaralah.');
load();setInterval(load,2000);
</script></body></html>)=====";

void setupRoutes() {
  server.on("/", []() { if (!auth()) return; server.send_P(200, "text/html", PAGE); });
  server.on("/api/status", []() { if (!auth()) return; sendStatus(); });
  server.on("/api/toggle", []() { if (!auth()) return; int c = chArg(); setRelay(c, !rs[c]); sendStatus(); });
  server.on("/api/set", []() { if (!auth()) return; setRelay(chArg(), server.arg("s") == "1"); sendStatus(); });
  server.on("/api/all", []() { if (!auth()) return; for (int i = 0; i < NUM_RELAY; i++) setRelay(i, server.arg("s") == "1"); sendStatus(); });
  server.on("/api/timer", []() {
    if (!auth()) return; int c = chArg(); long m = server.arg("min").toInt();
    if (m > 0) { setRelay(c, true); timerOn[c] = true; offAt[c] = millis() + (unsigned long)m * 60000UL; }
    else timerOn[c] = false;
    sendStatus();
  });
  server.on("/api/sched", []() {
    if (!auth()) return; int c = chArg();
    cfg.se[c] = server.arg("en") == "1";
    String on = server.arg("on"), off = server.arg("off");
    if (on.length() == 5) cfg.onMin[c] = on.substring(0, 2).toInt() * 60 + on.substring(3).toInt();
    if (off.length() == 5) cfg.offMin[c] = off.substring(0, 2).toInt() * 60 + off.substring(3).toInt();
    saveCfg(); sendStatus();
  });
  server.on("/api/pulse", []() {
    if (!auth()) return; int c = chArg();
    cfg.pulse[c] = constrain(server.arg("ms").toInt(), 0, 60000); saveCfg(); sendStatus();
  });
  server.on("/api/name", []() {
    if (!auth()) return; int c = chArg(); String n = server.arg("n");
    n.replace("\"", ""); n.replace("\\", ""); n.trim();
    if (n.length() == 0) n = "Relay " + String(c + 1);
    n.substring(0, 15).toCharArray(cfg.name[c], 16); saveCfg(); sendStatus();
  });
  server.on("/api/restore", []() { if (!auth()) return; cfg.restore = server.arg("v") == "1"; saveCfg(); sendStatus(); });
  server.on("/api/time", []() {
    if (!auth()) return;
    baseSec = server.arg("s").toInt() % 86400; baseMs = millis(); synced = true; lastMinute = -1;
    sendStatus();
  });
}

void setup() {
  for (int i = 0; i < 4; i++) pinMode(PINS[i], INPUT);
  EEPROM.begin(sizeof(cfg));
  EEPROM.get(0, cfg);
  if (cfg.magic != 0xA5) defaultCfg();
  for (int i = 0; i < NUM_RELAY; i++) { rs[i] = cfg.restore ? cfg.state[i] : false; applyRelay(i); }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  setupRoutes();
  updater.setup(&server, "/update", WEB_USER, WEB_PASS);
  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();
  for (int i = 0; i < NUM_RELAY; i++) {
    if (timerOn[i] && (long)(now - offAt[i]) >= 0) setRelay(i, false);
    if (pulseOn[i] && (long)(now - pulseUntil[i]) >= 0) { rs[i] = false; pulseOn[i] = false; applyRelay(i); }
  }
  if (synced) {
    int m = secOfDay() / 60;
    if (m != lastMinute) {
      lastMinute = m;
      for (int i = 0; i < NUM_RELAY; i++) {
        if (!cfg.se[i]) continue;
        if (m == cfg.onMin[i]) setRelay(i, true);
        else if (m == cfg.offMin[i]) setRelay(i, false);
      }
    }
  }
}
