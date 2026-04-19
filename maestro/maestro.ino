#include <WiFi.h>
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
    String url = "http://" + workers[i].ip + "/health";
    http.begin(url);
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

String proxyToWorker(String path) {
  int idx = getNextWorker();
  if (idx == -1) return "<h1>503 — All workers are down</h1>";

  HTTPClient http;
  String url = "http://" + workers[idx].ip + path;
  http.begin(url);
  http.setTimeout(3000);

  int code = http.GET();
  String response = "";
  if (code > 0) {
    response = http.getString();
    totalRequests++;
  } else {
    response = "<h1>502 — Worker " + String(workers[idx].id) + " failed to respond</h1>";
    workers[idx].healthy = false;
  }

  http.end();
  return response;
}

String buildDashboard() {
  int healthyCount = 0;
  for (int i = 0; i < 3; i++) {
    if (workers[i].healthy) healthyCount++;
  }

  String html = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>ESP32 Mini Datacenter</title>
<style>
  body { font-family: 'Courier New', monospace; background: #0a0a0a; color: #00ff41; margin: 0; padding: 20px; }
  h1 { text-align: center; font-size: 1.5em; border-bottom: 1px solid #00ff41; padding-bottom: 10px; }
  .grid { display: flex; flex-wrap: wrap; gap: 15px; justify-content: center; margin-top: 20px; }
  .node { background: #111; border: 1px solid #333; border-radius: 8px; padding: 15px; width: 250px; }
  .node.healthy { border-color: #00ff41; }
  .node.down { border-color: #ff0040; }
  .status { font-size: 1.2em; font-weight: bold; }
  .healthy .status { color: #00ff41; }
  .down .status { color: #ff0040; }
  .stat { margin: 5px 0; font-size: 0.9em; color: #aaa; }
  .gateway-stats { text-align: center; margin: 20px 0; color: #aaa; font-size: 0.9em; line-height: 1.8; }
  .net-info { text-align: center; margin: 10px 0; padding: 10px; background: #111; border: 1px solid #333; border-radius: 8px; }
  .net-info span { color: #00ff41; }
</style>
</head><body>
<h1>// ESP32-C6 MINI DATACENTER //</h1>

<div class='net-info'>
  Cluster Network: <span>)rawliteral" + String(CLUSTER_SSID) + R"rawliteral(</span><br>
  Gateway (internal): <span>192.168.4.1</span><br>
  Gateway (external): <span>)rawliteral" + homeIP + R"rawliteral(</span><br>
  Workers online: <span>)rawliteral" + String(healthyCount) + R"rawliteral(/3</span>
</div>

<div class='gateway-stats'>
  Total proxied requests: )rawliteral" + String(totalRequests) + R"rawliteral(
  | Uptime: )rawliteral" + String((millis() - startTime) / 1000) + R"rawliteral(s
</div>
<div class='grid'>
)rawliteral";

  for (int i = 0; i < 3; i++) {
    String cls = workers[i].healthy ? "node healthy" : "node down";
    String st = workers[i].healthy ? "ONLINE" : "OFFLINE";
    html += "<div class='" + cls + "'>";
    html += "<div class='status'>Worker " + String(workers[i].id) + ": " + st + "</div>";
    html += "<div class='stat'>IP: " + workers[i].ip + " (cluster)</div>";
    html += "<div class='stat'>Uptime: " + String(workers[i].uptimeSec) + "s</div>";
    html += "<div class='stat'>Requests: " + String(workers[i].requestsServed) + "</div>";
    html += "<div class='stat'>Free heap: " + String(workers[i].freeHeap) + " bytes</div>";
    html += "</div>";
  }

  html += "</div>";
  html += "<script>setTimeout(()=>location.reload(), 5000);</script>";
  html += "</body></html>";
  return html;
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

void handleDashboard() {
  server.send(200, "text/html", buildDashboard());
}

void handleClusterAPI() {
  JsonDocument doc;
  doc["total_requests"] = totalRequests;
  doc["uptime_sec"] = (millis() - startTime) / 1000;
  doc["cluster_ssid"] = CLUSTER_SSID;
  doc["home_ip"] = homeIP;
  JsonArray arr = doc["workers"].to<JsonArray>();
  for (int i = 0; i < 3; i++) {
    JsonObject w = arr.add<JsonObject>();
    w["id"] = workers[i].id;
    w["ip"] = workers[i].ip;
    w["healthy"] = workers[i].healthy;
    w["uptime_sec"] = workers[i].uptimeSec;
    w["requests_served"] = workers[i].requestsServed;
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleProxy() {
  String path = server.uri();
  String body = proxyToWorker(path);
  server.send(200, "text/html", body);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-C6 GATEWAY ===");

  // --- 1. Start the cluster network ---
  WiFi.mode(WIFI_AP_STA);

  IPAddress clusterIP(GATEWAY_CLUSTER_IP);
  IPAddress clusterSubnet(CLUSTER_SUBNET);
  WiFi.softAPConfig(clusterIP, clusterIP, clusterSubnet);
  WiFi.softAP(CLUSTER_SSID, CLUSTER_PASSWORD, CLUSTER_CHANNEL, 0, 4);

  Serial.printf("Cluster network '%s' created\n", CLUSTER_SSID);
  Serial.printf("Gateway cluster IP: %s\n", WiFi.softAPIP().toString().c_str());

  // --- 2. Connect to external Wi-Fi ---
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
    Serial.printf("\nConnected to external Wi-Fi at %s\n", homeIP.c_str());
  } else {
    Serial.println("\nExternal Wi-Fi failed — dashboard only at 192.168.4.1");
  }

  // Give the network stack a moment to stabilize
  delay(1000);

  startTime = millis();

  // --- Routes ---
  server.on("/dashboard", handleDashboard);
  server.on("/api/cluster", handleClusterAPI);
  server.onNotFound(handleProxy);

  server.begin();
  Serial.println("Gateway server started.");
}

void loop() {
  server.handleClient();
  checkWorkerHealth();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("External Wi-Fi lost, reconnecting...");
    connectToExternalWifi();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      homeIP = WiFi.localIP().toString();
      Serial.printf("Reconnected at %s\n", homeIP.c_str());
    }
  }
  delay(10);
}