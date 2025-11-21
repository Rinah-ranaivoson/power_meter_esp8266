/*
Power Meter
by Tolojanahary 
Co-developed with Rina
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

#include <PZEM004Tv30.h>
#include <SoftwareSerial.h>

#include <Wire.h>
#include "SSD1306Wire.h"

// -------- HARDWARE CONFIG ----------
#define RELAY_PIN 13               // Relay pin (active LOW)
SSD1306Wire display(0x3c, 14, 12); // OLED I2C: address, SDA(GPIO), SCL(GPIO)

// PZEM using SoftwareSerial 
SoftwareSerial pzemSW(2, 0);       // RX, TX 
PZEM004Tv30 pzem(pzemSW);

// -------- WIFI AP ----------
const char* ap_ssid = "systemmpd";    //<<<<<<<<<< Acces point NAME
const char* ap_pass = "123456789";    //<<<<<<<<<< Acces point PASSWORD
ESP8266WebServer server(80);

// -------- ENERGY VARIABLES ----------
float energyMax = 0.0;   // total energy allocated (Wh)
float energyLeft = 0.0;  // remaining energy (Wh)

// EEPROM layout (store two floats)
#define EEPROM_SIZE 128
#define ADDR_ENERGY_MAX 0      // float (4 bytes)
#define ADDR_ENERGY_LEFT 4     // float (4 bytes)

// Time tracking for consumption calculation
unsigned long lastMillis = 0;

// -------- HTML PAGES (user + admin) ----------
const char* pageIndex = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Systeme Energie</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  body{font-family:Arial,Helvetica,sans-serif;background:#0f1720;color:#e6eef8;margin:0;padding:12px}
  .card{background:#102027;border-radius:8px;padding:12px;margin-bottom:12px;box-shadow:0 4px 12px rgba(0,0,0,0.5)}
  h1{font-size:18px;margin:0 0 8px 0}
  .row{display:flex;justify-content:space-between;align-items:center;margin-bottom:6px}
  .big{font-size:20px;font-weight:700}
  .small{font-size:12px;color:#a8b6c6}
  .btn{display:inline-block;background:#2563eb;color:white;padding:8px 12px;border-radius:6px;text-decoration:none}
  .btn.secondary{background:#64748b}
  .progress{height:16px;background:#0b1220;border-radius:8px;overflow:hidden}
  .bar{height:100%;background:linear-gradient(90deg,#10b981,#06b6d4);width:0%}
  .center{text-align:center}
  .adminLink{float:right;background:#111827;padding:6px 10px;border-radius:6px;color:#9fb3ff;text-decoration:none}
  input[type="text"]{width:100%;padding:8px;border-radius:6px;border:1px solid #223344;background:#071020;color:#e6eef8}
  .controls{display:flex;gap:8px}
  @media (min-width:480px){ .controls {max-width:420px} }
</style>
</head>
<body>
  <div class="card">
    <h1>Systeme Energie <a class="adminLink" href="/admin">Admin</a></h1>
    <div class="row"><div> Tension (V) </div><div id="voltage" class="big">--</div></div>
    <div class="row"><div> Courant (A) </div><div id="current" class="big">--</div></div>
    <div class="row"><div> Puissance (W) </div><div id="power" class="big">--</div></div>
    <div class="row"><div> Energie restante (Wh) </div><div id="energy" class="big">--</div></div>
    <div class="row"><div> Relais </div><div id="relay" class="big">--</div></div>
    <div style="margin-top:8px">
      <div class="progress"><div id="bar" class="bar"></div></div>
      <div class="center small" id="percent">0%</div>
    </div>
  </div>

  <div class="card center small">
    <a class="btn" id="refreshBtn" href="javascript:;">Rafraîchir</a>
    <a class="btn secondary" href="/">Accueil</a>
  </div>

<script>
function update(){
  fetch('/status').then(r=>r.json()).then(j=>{
    document.getElementById('voltage').innerText = j.voltage.toFixed(2);
    document.getElementById('current').innerText = j.current.toFixed(3);
    document.getElementById('power').innerText = j.power.toFixed(2);
    document.getElementById('energy').innerText = j.energy.toFixed(4);
    document.getElementById('relay').innerText = j.relay ? "ON" : "OFF";
    document.getElementById('bar').style.width = Math.min(100,Math.max(0,j.percent)) + "%";
    document.getElementById('percent').innerText = Math.round(j.percent) + "%";
  }).catch(e=>{
    console.log('err',e);
  });
}

document.getElementById('refreshBtn').addEventListener('click', update);
setInterval(update, 1500);
update();
</script>
</body>
</html>
)rawliteral";

const char* pageAdmin = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Admin - Systeme Energie</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  body{font-family:Arial,Helvetica,sans-serif;background:#071026;color:#e6eef8;margin:0;padding:16px}
  .card{background:#0f1720;border-radius:8px;padding:12px;margin-bottom:12px}
  h1{margin:0 0 10px 0}
  input[type="text"]{width:100%;padding:8px;border-radius:6px;border:1px solid #223344;background:#071020;color:#e6eef8;margin-bottom:8px}
  .btn{display:inline-block;background:#059669;color:white;padding:8px 12px;border-radius:6px;text-decoration:none;margin-right:6px}
  .btn.danger{background:#ef4444}
  .small{font-size:13px;color:#9fb3ff}
  .row{display:flex;gap:8px;align-items:center}
</style>
</head>
<body>
  <div class="card">
    <h1>Mode Admin</h1>
    <div class="small">Valeur actuelle d'énergie: <span id="curE">--</span> Wh</div>
    <div style="height:12px"></div>
    <input id="valInput" type="text" placeholder="Ex: 0.05">
    <div class="row">
      <a class="btn" href="javascript:add()">Ajouter</a>
      <a class="btn danger" href="javascript:sub()">Soustraire</a>
      <a class="btn" href="/">Retour</a>
    </div>
    <div style="height:12px"></div>
    <div class="small" id="msg"></div>
  </div>

<script>
function fetchEnergy(){
  fetch('/status').then(r=>r.json()).then(j=>{
    document.getElementById('curE').innerText = j.energy.toFixed(4);
  });
}
function add(){
  const v = document.getElementById('valInput').value;
  if(!v) { document.getElementById('msg').innerText='Entrez une valeur'; return; }
  fetch('/add?val=' + encodeURIComponent(v)).then(r=>r.json()).then(j=>{
    document.getElementById('msg').innerText = j.msg;
    fetchEnergy();
  });
}
function sub(){
  const v = document.getElementById('valInput').value;
  if(!v) { document.getElementById('msg').innerText='Entrez une valeur'; return; }
  fetch('/sub?val=' + encodeURIComponent(v)).then(r=>r.json()).then(j=>{
    document.getElementById('msg').innerText = j.msg;
    fetchEnergy();
  });
}
fetchEnergy();
</script>
</body>
</html>
)rawliteral";

// ---------- EEPROM helpers ----------
void writeFloatToEEPROM(int addrOffset, float value) {
  byte *ptr = (byte*)(void*)&value;
  for (unsigned int i = 0; i < sizeof(float); i++) {
    EEPROM.write(addrOffset + i, *ptr++);
  }
  EEPROM.commit();
}

float readFloatFromEEPROM(int addrOffset) {
  float value = 0.0;
  byte *ptr = (byte*)(void*)&value;
  for (unsigned int i = 0; i < sizeof(float); i++) {
    *ptr++ = EEPROM.read(addrOffset + i);
  }
  return value;
}

// ---------- Web handlers ----------
void handleRoot(){
  server.send(200, "text/html", pageIndex);
}

void handleAdmin(){
  server.send(200, "text/html", pageAdmin);
}

void handleStatus(){
  float V = pzem.voltage(); if (isnan(V)) V = 0;
  float A = pzem.current(); if (isnan(A)) A = 0;
  float W = pzem.power();   if (isnan(W)) W = 0;

  float percent = 0.0;
  if (energyMax > 0.0) percent = (energyLeft / energyMax) * 100.0;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  String json = "{";
  json += "\"voltage\":" + String(V, 4) + ",";
  json += "\"current\":" + String(A, 6) + ",";
  json += "\"power\":" + String(W, 4) + ",";
  json += "\"energy\":" + String(energyLeft, 6) + ",";
  json += "\"percent\":" + String(percent, 2) + ",";
  json += "\"relay\":" + String(digitalRead(RELAY_PIN) == LOW ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleAdd(){
  if (!server.hasArg("val")) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"missing val\"}");
    return;
  }
  String sval = server.arg("val");
  sval.trim();
  float v = sval.toFloat();
  if (isnan(v) || v <= 0.0) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"valeur invalide\"}");
    return;
  }

  // Update both energyMax and energyLeft immediately
  energyMax += v;
  energyLeft += v;

  // Guard against negative 
  if (energyMax < 0.0) energyMax = 0.0;
  if (energyLeft < 0.0) energyLeft = 0.0;

  // Ensure energyLeft does not exceed energyMax 
  if (energyLeft > energyMax) energyLeft = energyMax;

  // Persist
  writeFloatToEEPROM(ADDR_ENERGY_MAX, energyMax);
  writeFloatToEEPROM(ADDR_ENERGY_LEFT, energyLeft);

  String res = "{\"ok\":true,\"msg\":\"Ajouté: " + String(v, 6) + "\"}";
  server.send(200, "application/json", res);
}

void handleSub(){
  if (!server.hasArg("val")) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"missing val\"}");
    return;
  }
  String sval = server.arg("val");
  sval.trim();
  float v = sval.toFloat();
  if (isnan(v) || v <= 0.0) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"valeur invalide\"}");
    return;
  }

  // Subtract from both energyMax and energyLeft
  energyMax -= v;
  energyLeft -= v;

  // Prevent negative
  if (energyMax < 0.0) energyMax = 0.0;
  if (energyLeft < 0.0) energyLeft = 0.0;

  // Ensure energyLeft <= energyMax
  if (energyLeft > energyMax) energyLeft = energyMax;

  // Persist
  writeFloatToEEPROM(ADDR_ENERGY_MAX, energyMax);
  writeFloatToEEPROM(ADDR_ENERGY_LEFT, energyLeft);

  String res = "{\"ok\":true,\"msg\":\"Soustrait: " + String(v, 6) + "\"}";
  server.send(200, "application/json", res);
}

void handleNotFound(){
  server.send(404, "text/plain", "Not found");
}

// ---------- Helper functions ----------
void updateRelay(){
  // Relay active LOW
  if (energyLeft > 0.0) digitalWrite(RELAY_PIN, LOW);  // ON
  else digitalWrite(RELAY_PIN, HIGH);                  // OFF
}

int getProgressPercent(){
  if (energyMax <= 0.0) return 0;
  float p = (energyLeft / energyMax) * 100.0;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return (int)p;
}

void drawOLED(float V, float A, float W){
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  display.drawString(0, 0, String(V, 2) + " V " + "  " + String(A, 3) + " A ");
  display.drawString(0, 12, "P: " + String(W, 2) + " W");
  display.drawString(0, 24, "Left: " + String(energyLeft, 6) + " Wh");

  int pct = getProgressPercent();
  display.drawProgressBar(0, 50, 120, 10, pct);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 35, String(pct) + "%");

  display.display();
}

// ---------- setup ----------
void setup(){
  Serial.begin(115200);
  delay(50);

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);
  float storedMax = readFloatFromEEPROM(ADDR_ENERGY_MAX);
  float storedLeft = readFloatFromEEPROM(ADDR_ENERGY_LEFT);

  // Validation: accept values in reasonable range
  if (!isnan(storedMax) && storedMax >= 0.0 && storedMax < 1e7) {
    energyMax = storedMax;
  } else {
    energyMax = 0.0;
  }
  if (!isnan(storedLeft) && storedLeft >= 0.0 && storedLeft <= 1e7) {
    energyLeft = storedLeft;
  } else {
    energyLeft = energyMax; // if not valid, assume full
  }
  // Ensure consistency
  if (energyLeft > energyMax) energyLeft = energyMax;

  Serial.printf("Restored from EEPROM: max=%f left=%f\n", energyMax, energyLeft);

  // OLED init
  display.init();
  display.flipScreenVertically();

  // --- Écran d’accueil stylisé ---
  display.clear();

  // GRAND : Nom (centré)
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 8, "Tolojanahary");

  // Moyen : Description (centré)
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 35, "Power Meter");

  // Petit : Version (bas à droite)
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(128, 54, "v0.6");

  // Affiche l’écran
  display.display();

  // Pause 1 seconde
  delay(2000);


  // Relay init 
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  // Start AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP started. IP=");
  Serial.println(myIP);

  // Web handlers
  server.on("/", handleRoot);
  server.on("/admin", handleAdmin);
  server.on("/status", handleStatus);
  server.on("/add", handleAdd);
  server.on("/sub", handleSub);
  server.onNotFound(handleNotFound);
  server.begin();

  lastMillis = millis();
}

// ---------- loop ----------
void loop(){
  server.handleClient();

  // Read PZEM values (best effort)
  float V = pzem.voltage(); if (isnan(V)) V = 0;
  float A = pzem.current(); if (isnan(A)) A = 0;
  float W = pzem.power();   if (isnan(W)) W = 0;

  // compute delta time (seconds)
  unsigned long now = millis();
  float dt_seconds = (now - lastMillis) / 1000.0;
  lastMillis = now;

  // energy consumed in this interval (Wh) = P(W) * dt(h)
  if (W > 0.0 && dt_seconds > 0.0) {
    float consumed = W * (dt_seconds / 3600.0);
    energyLeft -= consumed;
    if (energyLeft < 0.0) energyLeft = 0.0;
  }

  // Ensure energyLeft <= energyMax
  if (energyLeft > energyMax) energyLeft = energyMax;

  if (energyLeft <= 0) {
    energyLeft = 0;
    energyMax = 0;  // 
}


  // Update relay
  updateRelay();

  // Update OLED
  drawOLED(V, A, W);

  delay(250);
}
