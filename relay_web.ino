#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

const char* SSID_NAME  = "WiFi Gratis";
const char* ADMIN_USER = "admin";
const char* ADMIN_PASS = "ren";   // ganti!

IPAddress apIP(192, 168, 4, 1);
DNSServer dns;
ESP8266WebServer server(80);

uint32_t victims[50];
int victimCount = 0;
File upFile;

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{margin:0;background:#111;color:#fff;font-family:sans-serif;text-align:center;
display:flex;flex-direction:column;justify-content:center;align-items:center;min-height:100vh}
button{font-size:22px;padding:16px 36px;border:0;border-radius:12px;background:#2ecc71;color:#fff}
#bar{width:70%;height:14px;background:#333;border-radius:8px;overflow:hidden;display:none}
#fill{height:100%;width:0;background:#2ecc71}
#prank{display:none;width:100%}
#prank img{width:100%}
.big{font-size:120px}
.flash{animation:f .25s infinite}
@keyframes f{0%{background:#e74c3c}50%{background:#f1c40f}100%{background:#e74c3c}}
</style></head><body>
<div id="awal"><h2>📶 WiFi Gratis</h2><p>Tekan untuk terhubung</p>
<button onclick="mulai()">Hubungkan</button></div>
<div id="load" style="display:none"><p id="t">Menghubungkan...</p>
<div id="bar" style="display:block"><div id="fill"></div></div></div>
<div id="prank"><div id="vis"></div><h1>KENA PRANK! 😝</h1></div>
<audio id="aud" loop></audio>
<script>
var info={foto:false,suara:false};
fetch('/info').then(r=>r.json()).then(j=>info=j);
var ctx,osc;
function siren(){
  ctx=new (window.AudioContext||window.webkitAudioContext)();
  osc=ctx.createOscillator();osc.type='square';
  var g=ctx.createGain();g.gain.value=0.4;
  osc.connect(g);g.connect(ctx.destination);osc.start();
  var hi=true;setInterval(function(){osc.frequency.value=hi?900:500;hi=!hi},300);
}
function mulai(){
  document.getElementById('awal').style.display='none';
  document.getElementById('load').style.display='block';
  var a=document.getElementById('aud');
  if(info.suara){a.src='/suara';a.play();a.pause();a.currentTime=0;} // unlock audio
  var p=0,msgs=['Menghubungkan...','Mendapatkan IP...','Memeriksa jaringan...','Hampir selesai...'];
  var iv=setInterval(function(){
    p+=2;document.getElementById('fill').style.width=p+'%';
    document.getElementById('t').innerText=msgs[Math.min(3,Math.floor(p/26))]+' '+p+'%';
    if(p>=100){clearInterval(iv);boom();}
  },80);
}
function boom(){
  fetch('/hit');
  document.getElementById('load').style.display='none';
  document.getElementById('prank').style.display='block';
  document.getElementById('vis').innerHTML=info.foto?'<img src="/foto">':'<div class="big">🤡</div>';
  document.body.className='flash';
  if(navigator.vibrate)navigator.vibrate([500,200,500,200,500,200,500]);
  if(info.suara){var a=document.getElementById('aud');a.volume=1;a.play();}else{siren();}
}
</script></body></html>
)rawliteral";

const char ADMIN[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{font-family:sans-serif;padding:16px;max-width:480px;margin:auto}
button,input{margin:6px 0;padding:10px;font-size:16px}.c{font-size:48px;font-weight:bold}</style></head><body>
<h2>Admin Prank WiFi</h2>
<div>Korban prank:</div><div class="c" id="n">-</div>
<button onclick="act('/reset')">Reset counter</button><hr>
<h3>Foto</h3><input type="file" id="f1" accept="image/*"><br>
<button onclick="up('foto','f1')">Upload foto</button>
<button onclick="act('/hapus?t=foto')">Hapus</button>
<h3>Suara (MP3)</h3><input type="file" id="f2" accept="audio/*"><br>
<button onclick="up('suara','f2')">Upload suara</button>
<button onclick="act('/hapus?t=suara')">Hapus</button><hr>
<p id="s"></p>
<script>
function load(){fetch('/count').then(r=>r.text()).then(x=>n.innerText=x);
fetch('/info').then(r=>r.json()).then(j=>s.innerText='Foto: '+(j.foto?'ada':'kosong')+' | Suara: '+(j.suara?'ada':'kosong'));}
function act(u){fetch(u).then(()=>load())}
function up(t,id){var f=document.getElementById(id).files[0];if(!f)return alert('Pilih file dulu');
var d=new FormData();d.append('file',f);
fetch('/upload?t='+t,{method:'POST',body:d}).then(r=>r.text()).then(x=>{alert(x);load()})}
load();setInterval(load,3000);
</script></body></html>
)rawliteral";

bool auth() {
  if (!server.authenticate(ADMIN_USER, ADMIN_PASS)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

void sendFile(const char* path, const char* type) {
  if (!LittleFS.exists(path)) { server.send(404, "text/plain", "kosong"); return; }
  File f = LittleFS.open(path, "r");
  server.streamFile(f, type);
  f.close();
}

void handleUpload() {
  HTTPUpload& u = server.upload();
  String path = (server.arg("t") == "foto") ? "/foto" : "/suara";
  if (u.status == UPLOAD_FILE_START) {
    upFile = LittleFS.open(path, "w");
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upFile) upFile.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    if (upFile) upFile.close();
  }
}

void setup() {
  LittleFS.begin();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(SSID_NAME);
  dns.start(53, "*", apIP);

  server.on("/", []() { server.send_P(200, "text/html", PAGE); });

  server.on("/info", []() {
    String j = "{\"foto\":" + String(LittleFS.exists("/foto") ? "true" : "false") +
               ",\"suara\":" + String(LittleFS.exists("/suara") ? "true" : "false") + "}";
    server.send(200, "application/json", j);
  });

  server.on("/foto",  []() { sendFile("/foto", "image/jpeg"); });
  server.on("/suara", []() { sendFile("/suara", "audio/mpeg"); });

  server.on("/hit", []() {
    uint32_t ip = (uint32_t)server.client().remoteIP();
    bool ada = false;
    for (int i = 0; i < victimCount; i++) if (victims[i] == ip) ada = true;
    if (!ada && victimCount < 50) victims[victimCount++] = ip;
    server.send(200, "text/plain", "ok");
  });

  server.on("/admin", []() { if (!auth()) return; server.send_P(200, "text/html", ADMIN); });
  server.on("/count", []() { if (!auth()) return; server.send(200, "text/plain", String(victimCount)); });
  server.on("/reset", []() { if (!auth()) return; victimCount = 0; server.send(200, "text/plain", "ok"); });
  server.on("/hapus", []() {
    if (!auth()) return;
    LittleFS.remove(server.arg("t") == "foto" ? "/foto" : "/suara");
    server.send(200, "text/plain", "ok");
  });
  server.on("/upload", HTTP_POST, []() {
    if (!auth()) return;
    server.send(200, "text/plain", "Upload selesai");
  }, []() { if (server.authenticate(ADMIN_USER, ADMIN_PASS)) handleUpload(); });

  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
}
