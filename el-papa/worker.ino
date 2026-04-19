#include <WiFi.h>
#include <ESPAsyncWebSrv.h>
#include <ArduinoJson.h>
#include "cluster_config.h"

// *** CHANGE THIS FOR EACH WORKER: 1, 2, or 3 ***
#define WORKER_ID 1

// Pick the matching IP
#if WORKER_ID == 1
  IPAddress staticIP(WORKER_1_IP);
#elif WORKER_ID == 2
  IPAddress staticIP(WORKER_2_IP);
#elif WORKER_ID == 3
  IPAddress staticIP(WORKER_3_IP);
#endif

IPAddress gateway(192, 168, 1, 1);    // your router IP
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer server(WORKER_PORT);

unsigned long requestCount = 0;
unsigned long startTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Connect to Wi-Fi with static IP
  // WiFi.config(staticIP, gateway, subnet);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Worker %d connecting to WiFi...\n", WORKER_ID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWorker %d online at %s\n", WORKER_ID, WiFi.localIP().toString().c_str());

  startTime = millis();

  // --- ROUTES ---

  // Main content route — this is what the gateway forwards visitors to
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    requestCount++;
    String html = "<!DOCTYPE html><html><body>";
    html += "<h1>Hello from Worker " + String(WORKER_ID) + "</h1>";
    html += "<p>This page was served by node " + String(WORKER_ID) + "</p>";
    html += "<p>Requests handled by this node: " + String(requestCount) + "</p>";
    html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Health check endpoint — the gateway pings this
  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["worker_id"] = WORKER_ID;
    doc["status"] = "healthy";
    doc["uptime_sec"] = (millis() - startTime) / 1000;
    doc["requests_served"] = requestCount;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["ip"] = WiFi.localIP().toString();

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // A sample API endpoint — shows the workers can host anything
  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    requestCount++;
    JsonDocument doc;
    doc["worker"] = WORKER_ID;
    doc["message"] = "Data from worker " + String(WORKER_ID);
    doc["timestamp"] = millis();

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.begin();
  Serial.printf("Worker %d web server started on port %d\n", WORKER_ID, WORKER_PORT);
}

void loop() {
  // Reconnect Wi-Fi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
    Serial.println("Reconnected.");
  }
  delay(1000);
}
