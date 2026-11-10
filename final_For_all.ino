#include <WiFi.h>
#include <WebServer.h>
#include <ModbusRTU.h>
#include <Update.h>
#include <Preferences.h>

#define RXD2 16
#define TXD2 17
#define RS485_EN 4
#define LED_PIN 2

// ================= WIFI =================
const char* apName = "MAGIC";
const char* apPassword = "12345678";

// ================= OTA LOGIN =================
// Change these credentials before deployment
const char* otaUser = "admin";
const char* otaPassword = "admin";

// ================= MODBUS =================
ModbusRTU mb;
WebServer server(80);

uint8_t slaveID = 3;
const uint16_t holdingRegister = 40001; // Fixed holding register
Preferences preferences;


// ================= SETTINGS =================
int minValue = 2;
int maxValue = 28;
int updateMinutes = 14;  // Minutes (0-59)
int updateSeconds = 0;  // Seconds (0-59); total interval must be positive

uint16_t randomValue = 1;

// Only one Modbus holding register is exposed.
// 40001 -> PDU address 0; 40002 -> PDU address 1, etc.
uint16_t modbusAddress(){ return 0; } // Fixed 40001, zero-based address 0

void initHoldingRegisters(){
  bool added = mb.addHreg(modbusAddress(), randomValue);
  Serial.printf("Modbus slave=%u register=%u raw address=%u mode=%u addHreg=%s\n", slaveID, holdingRegister, modbusAddress(), 0, added ? "OK" : "FAILED");
}

void updateHoldingRegisters(){
  mb.Hreg(modbusAddress(), randomValue);
}

unsigned long previousMillis = 0;
unsigned long ledStartMillis = 0;

bool ledBlinking = false;

// ================= OTA STATUS =================
bool otaSuccess = false;
bool otaFailed = false;
bool otaRestartPending = false;

unsigned long otaRestartMillis = 0;
bool configRestartPending = false;
unsigned long configRestartMillis = 0;

// Read and validate settings saved in ESP32 non-volatile storage.
void loadSavedSettings(){
  preferences.begin("magic", true);
  int a = preferences.getInt("zero", 2);
  int b = preferences.getInt("span", 28);
  int m = preferences.getInt("minutes", 14);
  int sec = preferences.getInt("seconds", 0);
  int id = preferences.getInt("slave", 3);
  preferences.end();
  if(a >= 0 && a <= b && b <= 65535){ minValue=a; maxValue=b; }
  if(m >= 0 && m <= 59 && sec >= 0 && sec <= 59 && (m+sec)>0){
    updateMinutes=m; updateSeconds=sec;
  }
  if(id >= 1 && id <= 247) slaveID=(uint8_t)id;
}

bool saveValues(int a,int b,int m,int sec){
  if(!preferences.begin("magic",false)) return false;
  preferences.putInt("zero",a);
  preferences.putInt("span",b);
  preferences.putInt("minutes",m);
  preferences.putInt("seconds",sec);
  bool ok = preferences.getInt("zero",-1)==a &&
            preferences.getInt("span",-1)==b &&
            preferences.getInt("minutes",-1)==m &&
            preferences.getInt("seconds",-1)==sec;
  preferences.end();
  return ok;
}

bool saveModbus(int id){
  if(!preferences.begin("magic",false)) return false;
  size_t written=preferences.putInt("slave",id);
  bool ok=written==sizeof(int32_t) && preferences.getInt("slave",-1)==id;
  preferences.end();
  return ok;
}

// ================= MAIN WEB PAGE =================
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport"
content="width=device-width,initial-scale=1">

<title>MAGIC | Modbus Controller</title>

<style>
*{
box-sizing:border-box;
}

body{
margin:0;
font-family:Arial,Helvetica,sans-serif;
background:linear-gradient(145deg,#dcefff,#f4fbff);
color:#14345d;
min-height:100vh;
}

header{
background:linear-gradient(120deg,#003c91,#008bd1);
padding:28px 15px;
text-align:center;
color:white;
border-radius:0 0 28px 28px;
}

header h1{
margin:0;
font-size:42px;
letter-spacing:4px;
}

header p{
margin:8px 0 0;
font-size:14px;
}

.container{
max-width:850px;
margin:25px auto;
padding:0 15px;
}

.card{
background:white;
border-radius:20px;
padding:24px;
margin-bottom:22px;
box-shadow:0 8px 25px #15508020;
}

.status{
display:inline-flex;
align-items:center;
gap:8px;
background:#e8f9ee;
color:#16853b;
padding:9px 14px;
border-radius:25px;
font-size:13px;
font-weight:bold;
}

.dot{
width:10px;
height:10px;
border-radius:50%;
background:#22c55e;
}

.offline{
background:#fff0f0;
color:#d33;
}

.offline .dot{
background:#e33;
}

.live-title{
text-align:center;
font-size:15px;
color:#487ba7;
font-weight:bold;
margin:25px 0 12px;
}

.value{
background:linear-gradient(125deg,#f50035,#ff9618);
border-radius:24px;
padding:28px 10px;
text-align:center;
font-size:clamp(65px,14vw,110px);
color:white;
font-weight:800;
box-shadow:0 10px 22px #ee653a35;
}

.unit{
text-align:center;
margin-top:12px;
font-weight:bold;
color:#6784a0;
}

.info-grid{
display:grid;
grid-template-columns:repeat(3,1fr);
gap:10px;
margin-top:25px;
}

.info{
background:#edf6ff;
border-radius:12px;
text-align:center;
padding:16px 5px;
}

.info small{
display:block;
font-size:11px;
color:#6885a2;
margin-bottom:9px;
}

.info strong{
color:#0756a8;
font-size:17px;
}

.settings-title{
background:linear-gradient(110deg,#0063b6,#23a6e9);
padding:18px;
color:white;
border-radius:14px;
font-size:18px;
font-weight:bold;
margin-bottom:24px;
}

.fields{
display:grid;
grid-template-columns:1fr;
gap:15px;
}

.field label{
display:block;
font-size:15px;
font-weight:bold;
margin-bottom:6px;
}

.field small{
display:block;
font-size:12px;
color:#72879c;
margin-bottom:9px;
}

input{
width:100%;
padding:14px;
border:1.5px solid #d1e0f0;
border-radius:12px;
font-size:19px;
font-weight:bold;
background:#f8fbff;
color:#173c68;
outline:none;
}

select{
width:100%; padding:14px; border:1.5px solid #d1e0f0;
border-radius:12px; font-size:19px; font-weight:bold;
background:#f8fbff; color:#173c68;
}

input:focus{
border-color:#168ee2;
box-shadow:0 0 0 3px #168ee220;
}

button{
width:100%;
padding:17px;
margin-top:22px;
background:linear-gradient(120deg,#2acc36,#078b27);
border:none;
border-radius:13px;
font-size:17px;
font-weight:bold;
color:white;
cursor:pointer;
}

button:disabled{
opacity:.6;
cursor:wait;
}

.message{
text-align:center;
margin-top:16px;
font-size:14px;
font-weight:bold;
min-height:18px;
}

.success{color:green}
.error{color:red}

footer{
text-align:center;
padding:15px 10px 30px;
font-size:13px;
color:#527493;
line-height:1.8;
}

@media(min-width:650px){
.fields{
grid-template-columns:repeat(2,1fr);
}
}

@media(max-width:380px){
.card{padding:16px}
.info strong{font-size:14px}
}
</style>
</head>

<body>

<header>
<h1>MAGIC</h1>
<p>ESP32 MODBUS RTU CONTROLLER</p>
</header>

<div class="container">

<div class="card">

<div class="status" id="status">
<span class="dot"></span>
<span id="statusText">CONNECTING...</span>
</div>

<div class="live-title">LIVE SPM VALUE</div>

<div class="value" id="liveValue">--</div>

<div class="unit">SPM</div>

<div class="info-grid">

<div class="info">
<small>SLAVE ID</small>
<strong id="slaveInfo">3</strong>
</div>

<div class="info">
<small>REGISTER</small>
<strong id="registerInfo">40001</strong>
</div>

<div class="info">
<small>UPDATE TIME</small>
<strong id="interval">0 min 2 sec</strong>
</div>

</div>

</div>

<div class="card">

<div class="settings-title">
&#9881; PARAMETER SETTINGS
</div>

<form id="settingsForm">

<div class="fields">

<div class="field">
<label>ZERO</label>
<small>Minimum SPM Value</small>
<input type="number" id="min"
min="0" max="65535" required>
</div>

<div class="field">
<label>SPAN</label>
<small>Maximum SPM Value</small>
<input type="number" id="max"
min="0" max="65535" required>
</div>

<div class="field">
<label>MINUTES (min)</label>
<small>Update interval minutes (0–59)</small>
<input type="number" id="minutes" min="0" max="59" value="0" required>
</div>

<div class="field">
<label>SECONDS (sec)</label>
<small>Update interval seconds (0–59)</small>
<input type="number" id="seconds" min="0" max="59" value="2" required>
</div>

</div>

<button type="submit" id="saveBtn">
SAVE SETTINGS
</button>

<div class="message" id="message"></div>

</form>

</div>

</div>

<footer>
<b>2026-2027 &copy; SOURAV</b>
<div>All rights reserved.</div>
</footer>

<script>

const $ = id => document.getElementById(id);

function setStatus(online){
$("status").classList.toggle("offline",!online);
$("statusText").textContent =
online ? "DEVICE CONNECTED" : "CONNECTION LOST";
}

async function updateValue(){
try{
const response=await fetch("/value",
{cache:"no-store"});

if(!response.ok) throw Error("Offline");

$("liveValue").textContent=await response.text();
setStatus(true);

}catch(e){
setStatus(false);
}
}

async function loadSettings(){
try{
const response=await fetch("/settings",
{cache:"no-store"});

if(!response.ok) throw Error("Failed");

const data=await response.json();

$("min").value=data.min;
$("max").value=data.max;
$("minutes").value=data.minutes;
  $("seconds").value=data.seconds;
   $("slaveInfo").textContent=data.slave;
   $("registerInfo").textContent=data.register;
$("interval").textContent=data.minutes+" min "+data.seconds+" sec";

}catch(e){
$("message").textContent="Settings load failed";
$("message").className="message error";
}
}

$("settingsForm").addEventListener("submit",async(e)=>{
e.preventDefault();

const min=Number($("min").value);
const max=Number($("max").value);
const minutes=Number($("minutes").value);
const seconds=Number($("seconds").value);

if(!Number.isInteger(minutes) || !Number.isInteger(seconds) ||
minutes<0 || minutes>59 || seconds<0 || seconds>59 ||
minutes+seconds===0){
$("message").textContent="Enter 0–59 min and 0–59 sec; total must exceed zero";
$("message").className="message error";
return;
}

if(min>max){
$("message").textContent=
"ZERO cannot exceed SPAN";
$("message").className="message error";
return;
}

const btn=$("saveBtn");
btn.disabled=true;
btn.textContent="SAVING...";

try{
const params=new URLSearchParams({
min:min,max:max,minutes:minutes,seconds:seconds
});

const response=await fetch("/set?"+params.toString());
const result=await response.text();

if(!response.ok) throw Error(result);

$("interval").textContent=minutes+" min "+seconds+" sec";
$("message").textContent="Settings Saved Successfully!";
$("message").className="message success";

}catch(err){
$("message").textContent="Save Failed";
$("message").className="message error";
}

btn.disabled=false;
btn.textContent="SAVE SETTINGS";
});

loadSettings();
updateValue();
setInterval(updateValue,500);

</script>
</body>
</html>
)rawliteral";

// ================= MODBUS SETTINGS WEB PAGE =================
// Separate configuration UI at http://192.168.4.1/set
const char modbusPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MAGIC | Slave Settings</title>
<style>*{box-sizing:border-box}body{margin:0;font-family:Arial,sans-serif;background:linear-gradient(145deg,#dcefff,#f4fbff);color:#14345d}header{background:linear-gradient(120deg,#003c91,#008bd1);padding:26px;text-align:center;color:white;border-radius:0 0 25px 25px}header h1{margin:0;font-size:38px}header p{margin:8px 0 0}.card{max-width:550px;margin:30px auto;background:white;padding:26px;border-radius:20px;box-shadow:0 8px 25px #15508020}label{display:block;font-weight:bold;margin:18px 0 7px}input{width:100%;padding:14px;border:1.5px solid #d1e0f0;border-radius:12px;font-size:19px}small{display:block;color:#6885a2;margin-top:5px}button{width:100%;padding:16px;margin-top:24px;background:#078b27;color:white;border:0;border-radius:12px;font-size:17px;font-weight:bold;cursor:pointer}.message{min-height:25px;margin-top:15px;text-align:center;font-weight:bold}a{color:#0756a8}</style>
</head><body><header><h1>MAGIC</h1><p>MODBUS RTU SLAVE CONFIGURATION</p></header>
<div class="card"><h2>Slave ID Settings</h2>
<form id="f"><label for="slave">SLAVE ID</label><input type="number" id="slave" min="1" max="247" required>
<small>Valid Modbus Slave ID: 1 to 247</small>
<p>Fixed Holding Register: <strong>40001</strong> (zero-based Modbus address 0)</p>
<button id="save" type="submit">SAVE SLAVE ID</button><div id="msg" class="message"></div></form>
<p><small>ESP32 will restart after saving. Slave ID is saved to non-volatile memory.</small></p>
<p><a href="/">Back to main dashboard</a></p></div>
<script>
const $=id=>document.getElementById(id);
async function load(){try{const r=await fetch('/settings',{cache:'no-store'});if(!r.ok)throw Error();const d=await r.json();$('slave').value=d.slave;}catch(e){$('msg').textContent='Could not load settings';}}
$('f').addEventListener('submit',async e=>{e.preventDefault();const slave=Number($('slave').value);if(!Number.isInteger(slave)||slave<1||slave>247){$('msg').textContent='Invalid Slave ID';return;}$('save').disabled=true;try{const r=await fetch('/set-modbus',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({slave:slave})});const t=await r.text();if(!r.ok)throw Error(t);$('msg').textContent=t;}catch(err){$('msg').textContent='Save failed: '+err.message;$('save').disabled=false;}});
load();
</script></body></html>
)rawliteral";

// ================= HIDDEN OTA PAGE =================
const char otaPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport"
content="width=device-width,initial-scale=1">

<title>MAGIC | Firmware Update</title>

<style>
*{box-sizing:border-box}

body{
margin:0;
padding:20px 14px;
font-family:Arial,sans-serif;
background:linear-gradient(145deg,#dcefff,#f5fbff);
color:#16365d;
}

.card{
max-width:460px;
margin:35px auto;
background:white;
padding:28px;
border-radius:20px;
box-shadow:0 10px 30px #15508025;
}

h1{
text-align:center;
color:#0759ad;
font-size:36px;
margin-bottom:5px;
}

.subtitle{
text-align:center;
color:#71849a;
font-size:14px;
margin-bottom:30px;
}

input[type=file]{
width:100%;
padding:15px;
margin:15px 0;
border:2px dashed #90bce4;
border-radius:12px;
background:#f5faff;
}

button{
width:100%;
background:linear-gradient(120deg,#0078dc,#004b9f);
color:white;
border:none;
padding:17px;
border-radius:12px;
font-size:17px;
font-weight:bold;
cursor:pointer;
}

button:disabled{opacity:.5}

.progress{
width:100%;
height:17px;
background:#e4edf6;
border-radius:20px;
overflow:hidden;
margin-top:25px;
}

.bar{
height:100%;
width:0%;
background:linear-gradient(90deg,#008dff,#13c95c);
transition:width .15s;
}

#percent{
text-align:center;
font-weight:bold;
}

#status{
text-align:center;
font-size:14px;
margin-top:18px;
}

.warning{
background:#fff4dd;
padding:14px;
border-radius:10px;
font-size:13px;
margin-top:25px;
line-height:1.6;
}

footer{
text-align:center;
color:#6a8096;
font-size:12px;
margin-top:25px;
}
</style>
</head>

<body>

<div class="card">

<h1>MAGIC</h1>

<p class="subtitle">
ESP32 FIRMWARE UPDATE CENTER
</p>

<h3>Upload New Firmware</h3>

<p>Select your compiled ESP32 firmware file (.bin).</p>

<input type="file" id="firmware" accept=".bin">

<button id="uploadBtn" onclick="uploadFirmware()">
UPLOAD & UPDATE
</button>

<div class="progress">
<div class="bar" id="bar"></div>
</div>

<p id="percent">0%</p>

<p id="status">Ready to upload firmware</p>

<div class="warning">
<b>IMPORTANT</b><br>
Do not disconnect ESP32 power during firmware update.
After successful installation, ESP32 will restart automatically.
</div>

<footer>
2026-2027 &copy; SOURAV<br>
All rights reserved.
</footer>

</div>

<script>

function uploadFirmware(){

const file=document.getElementById("firmware").files[0];
const status=document.getElementById("status");
const btn=document.getElementById("uploadBtn");

if(!file || !file.name.toLowerCase().endsWith(".bin")){
status.textContent="Please select a .bin firmware file";
return;
}

btn.disabled=true;
status.textContent="Uploading firmware...";

const form=new FormData();
form.append("firmware",file,file.name);

const xhr=new XMLHttpRequest();
xhr.open("POST","/ota-upload",true);

xhr.upload.onprogress=function(e){
if(e.lengthComputable){
const p=Math.round(e.loaded*100/e.total);
document.getElementById("bar").style.width=p+"%";
document.getElementById("percent").textContent=p+"%";
}
};

xhr.onload=function(){
if(xhr.status===200){
status.textContent=
"Firmware updated! ESP32 restarting...";
}else{
status.textContent="Update failed: "+xhr.responseText;
btn.disabled=false;
}
};

xhr.onerror=function(){
status.textContent="Upload connection lost";
btn.disabled=false;
};

xhr.send(form);
}

</script>
</body>
</html>
)rawliteral";

// ================= MAIN HANDLERS =================
void handleRoot(){
server.send_P(200,"text/html",webpage);
}

void handleValue(){
server.send(200,"text/plain",String(randomValue));
}

void handleSettings(){

String json="{";
json+="\"min\":"+String(minValue)+",";
json+="\"max\":"+String(maxValue)+",";
json+="\"minutes\":"+String(updateMinutes)+",";
  json+="\"seconds\":"+String(updateSeconds)+",";
  json+="\"slave\":"+String(slaveID)+",";
  json+="\"register\":"+String(holdingRegister)+",";
    json+="\"rawAddress\":"+String(modbusAddress());
json+="}";

server.send(200,"application/json",json);
}

void handleSet(){

  // Opening /set without parameters displays the Modbus configuration page.
  if(server.args()==0){
    server.send_P(200,"text/html",modbusPage);
    return;
  }

  if(!server.hasArg("min") ||
!server.hasArg("max") ||
!server.hasArg("minutes") ||
!server.hasArg("seconds")){
server.send(400,"text/plain","Missing settings");
return;
}

long newMin=server.arg("min").toInt();
long newMax=server.arg("max").toInt();
long newMinutes=server.arg("minutes").toInt();
long newSeconds=server.arg("seconds").toInt();

if(newMin<0 || newMax<0 || newMin>65535 || newMax>65535 ||
newMin>newMax ||
newMinutes<0 || newMinutes>59 ||
newSeconds<0 || newSeconds>59 ||
(newMinutes==0 && newSeconds==0)){

server.send(400,"text/plain","Invalid settings");
return;
}

  if(!saveValues((int)newMin,(int)newMax,(int)newMinutes,(int)newSeconds)){
    server.send(500,"text/plain","Failed to save to NVS");
    return;
  }

  minValue=(int)newMin;
  maxValue=(int)newMax;
updateMinutes=(int)newMinutes;
updateSeconds=(int)newSeconds;

previousMillis=millis();

Serial.println("Settings Updated");

server.send(200,"text/plain","OK");
}

// ================= MODBUS CONFIG API =================
void handleModbusSave(){
  if(!server.hasArg("slave")){
    server.send(400,"text/plain","Missing Slave ID"); return;
  }
  String sid=server.arg("slave");
  if(sid.length()==0){server.send(400,"text/plain","Invalid Slave ID");return;}
  for(unsigned int i=0;i<sid.length();i++) if(!isDigit(sid[i])){
    server.send(400,"text/plain","Invalid Slave ID"); return;
  }
  long id=sid.toInt();
  if(id<1 || id>247){server.send(400,"text/plain","Slave ID must be 1-247");return;}
  if(!saveModbus((int)id)){
    server.send(500,"text/plain","Failed to save to NVS"); return;
  }
  server.send(200,"text/plain","Slave ID saved. ESP32 restarting...");
  configRestartMillis=millis();
  configRestartPending=true;
}

// ================= OTA AUTH =================
bool checkOTAAuth(){

if(!server.authenticate(otaUser,otaPassword)){
server.requestAuthentication();
return false;
}

return true;
}

// ================= OTA PAGE =================
void handleOTAPage(){

if(!checkOTAAuth()) return;

server.send_P(200,"text/html",otaPage);
}

// ================= OTA UPLOAD =================
void handleOTAUpload(){

if(!server.authenticate(otaUser,otaPassword)){
return;
}

HTTPUpload& upload=server.upload();

if(upload.status==UPLOAD_FILE_START){

otaSuccess=false;
otaFailed=false;

Serial.println("OTA Upload Started");

if(!Update.begin(UPDATE_SIZE_UNKNOWN)){
otaFailed=true;
Update.printError(Serial);
}

}else if(upload.status==UPLOAD_FILE_WRITE){

if(!otaFailed && !Update.hasError()){

if(Update.write(upload.buf,upload.currentSize)
!=upload.currentSize){

otaFailed=true;
Update.printError(Serial);
}
}

}else if(upload.status==UPLOAD_FILE_END){

if(!otaFailed && Update.end(true)){
otaSuccess=true;
Serial.println("OTA Update Successful");
}else{
otaFailed=true;
Update.printError(Serial);
}

}else if(upload.status==UPLOAD_FILE_ABORTED){

Update.abort();
otaFailed=true;
Serial.println("OTA Upload Aborted");
}
}

// ================= OTA RESULT =================
void handleOTAResult(){

if(!checkOTAAuth()) return;

if(otaSuccess && !otaFailed){

server.send(200,"text/plain",
"Firmware updated successfully");

otaRestartMillis=millis();
otaRestartPending=true;

}else{

server.send(500,"text/plain",
"Firmware update failed");
}
}

// ================= SETUP =================
void setup(){

  Serial.begin(115200);
  loadSavedSettings();

// BLUE LED
pinMode(LED_PIN,OUTPUT);
digitalWrite(LED_PIN,LOW);

// RS485
pinMode(RS485_EN,OUTPUT);
digitalWrite(RS485_EN,LOW);

Serial2.begin(9600,SERIAL_8N1,RXD2,TXD2);

  // MODBUS SLAVE ID (loaded from NVS)
mb.begin(&Serial2,RS485_EN);
mb.slave(slaveID);

  // Initialize fixed holding register 40001 only.
  initHoldingRegisters();

randomSeed(micros());

// WIFI ACCESS POINT
WiFi.mode(WIFI_AP);
WiFi.softAP(apName,apPassword);

Serial.println("WiFi Started");
Serial.print("IP Address: ");
Serial.println(WiFi.softAPIP());

// MAIN DASHBOARD
server.on("/",HTTP_GET,handleRoot);
server.on("/value",HTTP_GET,handleValue);
server.on("/settings",HTTP_GET,handleSettings);
  server.on("/set",HTTP_GET,handleSet);
  server.on("/set-modbus",HTTP_POST,handleModbusSave);

// HIDDEN OTA PAGE
server.on("/up",HTTP_GET,handleOTAPage);

// FIRMWARE UPLOAD
server.on("/ota-upload",HTTP_POST,
handleOTAResult,handleOTAUpload);

server.begin();

Serial.println("Web Server Started");
  Serial.println("Main: http://192.168.4.1");
  Serial.println("Modbus config: http://192.168.4.1/set");
Serial.println("OTA:  http://192.168.4.1/up");
}

// ================= LOOP =================
void loop(){

mb.task();
server.handleClient();

unsigned long now=millis();

// LED OFF AFTER 100ms
if(ledBlinking && now-ledStartMillis>=100){

digitalWrite(LED_PIN,LOW);
ledBlinking=false;
}

// GENERATE RANDOM VALUE
if(now-previousMillis >=
((unsigned long)updateMinutes * 60000UL +
(unsigned long)updateSeconds * 1000UL)){

previousMillis=now;

randomValue=(uint16_t)random(
(long)minValue,
(long)maxValue+1
);

// UPDATE MODBUS REGISTER
   updateHoldingRegisters();

// BLINK BLUE LED
digitalWrite(LED_PIN,HIGH);
ledStartMillis=now;
ledBlinking=true;

Serial.print("SPM: ");
Serial.println(randomValue);
}

  // Restart after saving new Modbus slave/register configuration.
  if(configRestartPending && millis()-configRestartMillis>=1500UL){
    Serial.println("Applying Modbus settings: restarting...");
    Serial.flush();
    ESP.restart();
  }

  // RESTART AFTER OTA UPDATE
if(otaRestartPending &&
millis()-otaRestartMillis>=1500UL){

Serial.println("Restarting ESP32...");
Serial.flush();
ESP.restart();
}
}