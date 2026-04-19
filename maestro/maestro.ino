#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "esp_wpa2.h"
#include "cluster_config.h"

WebServer server(GATEWAY_PORT);

struct Worker {
  int id;
  String ip;
  bool healthy;
  unsigned long uptimeSec;
  unsigned long requestsServed;
  unsigned long freeHeap;
  unsigned long lastCheck;
};

Worker workers[3] = {
  {1, "192.168.4.11", false, 0, 0, 0, 0},
  {2, "192.168.4.12", false, 0, 0, 0, 0},
  {3, "192.168.4.13", false, 0, 0, 0, 0}
};

int currentWorker = 0;
unsigned long totalRequests = 0;
unsigned long chatRequests = 0;
unsigned long startTime = 0;
String homeIP = "not connected";

int getNextWorker() {
  for (int i = 0; i < 3; i++) {
    int idx = (currentWorker + i) % 3;
    if (workers[idx].healthy) {
      currentWorker = (idx + 1) % 3;
      return idx;
    }
  }
  return -1;
}

void checkWorkerHealth() {
  for (int i = 0; i < 3; i++) {
    if (millis() - workers[i].lastCheck < HEALTH_CHECK_INTERVAL) continue;

    HTTPClient http;
    http.begin("http://" + workers[i].ip + "/health");
    http.setTimeout(2000);

    int code = http.GET();
    if (code == 200) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);
      workers[i].healthy = true;
      workers[i].uptimeSec = doc["uptime_sec"];
      workers[i].requestsServed = doc["requests_served"];
      workers[i].freeHeap = doc["free_heap"];
    } else {
      workers[i].healthy = false;
    }
    workers[i].lastCheck = millis();
    http.end();
  }
}

void connectToExternalWifi() {
  WiFi.disconnect(true);
  delay(100);
  #ifdef EAP_IDENTITY
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)EAP_IDENTITY, strlen(EAP_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)EAP_IDENTITY, strlen(EAP_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)EAP_PASSWORD, strlen(EAP_PASSWORD));
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(HOME_WIFI_SSID);
  #else
    WiFi.begin(HOME_WIFI_SSID, HOME_WIFI_PASSWORD);
  #endif
}

// ============ CHAT PAGE ============

void handleChatPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>EL QUATRO AI</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',monospace;background:#0a0a0a;color:#00ff41;height:100vh;display:flex;flex-direction:column}
#hdr{padding:12px 20px;border-bottom:1px solid #333;text-align:center}
#hdr h1{font-size:1.3em;letter-spacing:2px}
#hdr .inf{font-size:0.75em;color:#666;margin-top:4px}
#hdr a{color:#00ff41}
#chat{flex:1;overflow-y:auto;padding:20px;display:flex;flex-direction:column;gap:14px}
.msg{max-width:85%;line-height:1.6;padding:10px 14px;border-radius:6px}
.msg.user{align-self:flex-end;background:#0d2b0d;border:1px solid #1a4a1a}
.msg.assistant{align-self:flex-start;background:#111;border:1px solid #333;color:#ccc}
.msg.system{align-self:center;color:#666;font-size:0.85em;font-style:italic;padding:6px}
.msg .lbl{font-size:0.7em;color:#555;margin-bottom:4px;text-transform:uppercase}
#bar{padding:14px 20px;border-top:1px solid #333;display:flex;gap:10px}
#inp{flex:1;background:#111;border:1px solid #333;color:#00ff41;padding:12px;font-family:inherit;font-size:1em;outline:none;border-radius:6px}
#inp:focus{border-color:#00ff41}
#btn{background:#00ff41;color:#0a0a0a;border:none;padding:12px 24px;font-family:inherit;font-weight:bold;cursor:pointer;border-radius:6px;font-size:1em}
#btn:disabled{background:#222;color:#444;cursor:wait}
.typing{align-self:flex-start;color:#555}
.typing span{animation:pulse 1.5s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
</style>
</head><body>
<div id='hdr'>
<h1>// EL QUATRO AI //</h1>
<div class='inf'>ESP32-C6 Cluster &bull; Powered by Groq &bull; <a href='/dashboard'>Dashboard</a></div>
</div>
<div id='chat'>
<div class='msg system'>Cluster online. Type a message to talk to the AI.</div>
</div>
<div id='bar'>
<input type='text' id='inp' placeholder='Ask anything...' autocomplete='off'>
<button id='btn' onclick='send()'>SEND</button>
</div>
<script>
const chat=document.getElementById('chat'),inp=document.getElementById('inp'),btn=document.getElementById('btn');
let hist=[];

function addMsg(role,text){
  const d=document.createElement('div');
  d.className='msg '+role;
  const lbl=role==='user'?'You':'AI';
  d.innerHTML='<div class="lbl">'+lbl+'</div>'+text.replace(/\n/g,'<br>');
  chat.appendChild(d);
  chat.scrollTop=chat.scrollHeight;
}

function showTyping(){
  const d=document.createElement('div');
  d.className='msg typing';d.id='typ';
  d.innerHTML='<span>thinking...</span>';
  chat.appendChild(d);
  chat.scrollTop=chat.scrollHeight;
}

function hideTyping(){const e=document.getElementById('typ');if(e)e.remove();}

async function send(){
  const t=inp.value.trim();
  if(!t)return;
  addMsg('user',t);
  inp.value='';
  btn.disabled=true;
  showTyping();

  hist.push({role:'user',content:t});

  try{
    const r=await fetch('/api/chat',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({messages:hist.slice(-6)})
    });
    const d=await r.json();
    hideTyping();
    if(d.error){addMsg('system','Error: '+d.error);}
    else{addMsg('assistant',d.response);hist.push({role:'assistant',content:d.response});}
  }catch(e){hideTyping();addMsg('system','Connection error: '+e.message);}
  btn.disabled=false;
  inp.focus();
}

inp.addEventListener('keydown',e=>{if(e.key==='Enter')send();});
inp.focus();
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

// ============ GROQ API HANDLER ============

void handleChat() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"POST only\"}");
    return;
  }

  String body = server.arg("plain");

  JsonDocument reqDoc;
  if (deserializeJson(reqDoc, body)) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  // Build Groq API payload
  JsonDocument groqDoc;
  groqDoc["model"] = GROQ_MODEL;
  groqDoc["max_tokens"] = 200;

  JsonArray groqMsgs = groqDoc["messages"].to<JsonArray>();

  // System prompt
  JsonObject sysMsg = groqMsgs.add<JsonObject>();
  sysMsg["role"] = "system";
  sysMsg["content"] = "You are a helpful AI assistant running on a cluster of ESP32-C6 microcontrollers called EL QUATRO. Keep responses concise, under 100 words.";

  // Conversation history from the browser
  JsonArray userMsgs = reqDoc["messages"].as<JsonArray>();
  for (JsonObject msg : userMsgs) {
    JsonObject m = groqMsgs.add<JsonObject>();
    m["role"] = msg["role"].as<String>();
    m["content"] = msg["content"].as<String>();
  }

  String payload;
  serializeJson(groqDoc, payload);

  Serial.println("Calling Groq API...");

  // HTTPS connection to Groq
  WiFiClientSecure wifiClient;
  wifiClient.setInsecure();

  HTTPClient https;
  https.begin(wifiClient, "https://api.groq.com/openai/v1/chat/completions");
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + String(GROQ_API_KEY));
  https.setTimeout(15000);

  int httpCode = https.POST(payload);

  if (httpCode <= 0) {
    Serial.printf("Groq connection failed: %s\n", https.errorToString(httpCode).c_str());
    server.send(502, "application/json", "{\"error\":\"Cannot reach Groq API\"}");
    https.end();
    return;
  }

  String responseBody = https.getString();
  https.end();

  Serial.printf("Groq responded with HTTP %d\n", httpCode);

  // Parse response
  JsonDocument resDoc;
  if (deserializeJson(resDoc, responseBody)) {
    server.send(500, "application/json", "{\"error\":\"Failed to parse Groq response\"}");
    return;
  }

  // Check for Groq error
  if (resDoc["error"].is<JsonObject>()) {
    String errMsg = resDoc["error"]["message"].as<String>();
    Serial.println("Groq error: " + errMsg);
    server.send(500, "application/json", "{\"error\":\"Groq API error\"}");
    return;
  }

  String answer = resDoc["choices"][0]["message"]["content"].as<String>();
  chatRequests++;

  JsonDocument outDoc;
  outDoc["response"] = answer;
  String out;
  serializeJson(outDoc, out);
  server.send(200, "application/json", out);

  Serial.println("Chat response sent.");
}

// ============ DASHBOARD ============

void handleDashboard() {
  int healthyCount = 0;
  for (int i = 0; i < 3; i++) {
    if (workers[i].healthy) healthyCount++;
  }

  String html = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>EL QUATRO Dashboard</title>
<style>
body{font-family:'Courier New',monospace;background:#0a0a0a;color:#00ff41;margin:0;padding:20px}
h1{text-align:center;font-size:1.5em;border-bottom:1px solid #00ff41;padding-bottom:10px}
.grid{display:flex;flex-wrap:wrap;gap:15px;justify-content:center;margin-top:20px}
.node{background:#111;border:1px solid #333;border-radius:8px;padding:15px;width:250px}
.node.healthy{border-color:#00ff41}
.node.down{border-color:#ff0040}
.status{font-size:1.2em;font-weight:bold}
.healthy .status{color:#00ff41}
.down .status{color:#ff0040}
.stat{margin:5px 0;font-size:0.9em;color:#aaa}
.info{text-align:center;margin:10px 0;padding:12px;background:#111;border:1px solid #333;border-radius:8px}
.info span{color:#00ff41}
.stats{text-align:center;margin:15px 0;color:#aaa;font-size:0.9em}
a{color:#00ff41}
</style>
</head><body>
<h1>// EL QUATRO DASHBOARD //</h1>
<div class='info'>
Cluster: <span>)rawliteral" + String(CLUSTER_SSID) + R"rawliteral(</span> |
Gateway: <span>)rawliteral" + homeIP + R"rawliteral(</span> |
Workers: <span>)rawliteral" + String(healthyCount) + R"rawliteral(/3</span><br>
<a href='/'>Open Chat</a>
</div>
<div class='stats'>
Uptime: )rawliteral" + String((millis() - startTime) / 1000) + R"rawliteral(s |
Chat requests: )rawliteral" + String(chatRequests) + R"rawliteral( |
Free heap: )rawliteral" + String(ESP.getFreeHeap()) + R"rawliteral( bytes
</div>
<div class='grid'>
)rawliteral";

  for (int i = 0; i < 3; i++) {
    String cls = workers[i].healthy ? "node healthy" : "node down";
    String st = workers[i].healthy ? "ONLINE" : "OFFLINE";
    html += "<div class='" + cls + "'>";
    html += "<div class='status'>Worker " + String(workers[i].id) + ": " + st + "</div>";
    html += "<div class='stat'>IP: " + workers[i].ip + "</div>";
    html += "<div class='stat'>Uptime: " + String(workers[i].uptimeSec) + "s</div>";
    html += "<div class='stat'>Requests: " + String(workers[i].requestsServed) + "</div>";
    html += "<div class='stat'>Free heap: " + String(workers[i].freeHeap) + " bytes</div>";
    html += "</div>";
  }

  html += "</div><script>setTimeout(()=>location.reload(),5000);</script></body></html>";
  server.send(200, "text/html", html);
}

void handleClusterAPI() {
  JsonDocument doc;
  doc["total_requests"] = totalRequests;
  doc["chat_requests"] = chatRequests;
  doc["uptime_sec"] = (millis() - startTime) / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  JsonArray arr = doc["workers"].to<JsonArray>();
  for (int i = 0; i < 3; i++) {
    JsonObject w = arr.add<JsonObject>();
    w["id"] = workers[i].id;
    w["ip"] = workers[i].ip;
    w["healthy"] = workers[i].healthy;
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ============ SETUP & LOOP ============

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== EL QUATRO GATEWAY ===");

  WiFi.mode(WIFI_AP_STA);

  // Start cluster network
  IPAddress clusterIP(GATEWAY_CLUSTER_IP);
  IPAddress clusterSubnet(CLUSTER_SUBNET);
  WiFi.softAPConfig(clusterIP, clusterIP, clusterSubnet);
  WiFi.softAP(CLUSTER_SSID, CLUSTER_PASSWORD, CLUSTER_CHANNEL, 0, 4);
  Serial.printf("Cluster network '%s' created\n", CLUSTER_SSID);

  // Connect to external Wi-Fi
  connectToExternalWifi();
  Serial.print("Connecting to external Wi-Fi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    homeIP = WiFi.localIP().toString();
    Serial.printf("\nConnected at %s\n", homeIP.c_str());
  } else {
    Serial.println("\nExternal Wi-Fi failed — chat won't work (no Groq access)");
  }

  delay(1000);
  startTime = millis();

  // Routes
  server.on("/", handleChatPage);
  server.on("/api/chat", handleChat);
  server.on("/dashboard", handleDashboard);
  server.on("/api/cluster", handleClusterAPI);

  server.begin();
  Serial.println("Gateway server started.");
  Serial.printf("Chat:      http://%s/\n", homeIP.c_str());
  Serial.printf("Dashboard: http://%s/dashboard\n", homeIP.c_str());
}

void loop() {
  server.handleClient();
  checkWorkerHealth();

  if (WiFi.status() != WL_CONNECTED) {
    connectToExternalWifi();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      homeIP = WiFi.localIP().toString();
    }
  }
  delay(10);
}