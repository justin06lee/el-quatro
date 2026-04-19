#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "cluster_config.h"

// *** CHANGE THIS FOR EACH WORKER: 1, 2, or 3 ***
#define WORKER_ID 3

#if WORKER_ID == 1
IPAddress staticIP(WORKER_1_IP);
#elif WORKER_ID == 2
IPAddress staticIP(WORKER_2_IP);
#elif WORKER_ID == 3
IPAddress staticIP(WORKER_3_IP);
#endif

IPAddress clusterGateway(GATEWAY_CLUSTER_IP);
IPAddress subnet(CLUSTER_SUBNET);

WebServer server(WORKER_PORT);

unsigned long requestCount = 0;
unsigned long startTime = 0;

void handleRoot() {
  requestCount++;
  String html = "<!DOCTYPE html><html><body>";
  html += "<h1>Hello from Worker " + String(WORKER_ID) + "</h1>";
  html += "<p>Served by node " + String(WORKER_ID) + " on cluster network</p>";
  html += "<p>Requests handled: " + String(requestCount) + "</p>";
  html += "<p>Internal IP: " + WiFi.localIP().toString() + "</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleHealth() {
  JsonDocument doc;
  doc["worker_id"] = WORKER_ID;
  doc["status"] = "healthy";
  doc["uptime_sec"] = (millis() - startTime) / 1000;
  doc["requests_served"] = requestCount;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["ip"] = WiFi.localIP().toString();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleData() {
  requestCount++;
  JsonDocument doc;
  doc["worker"] = WORKER_ID;
  doc["message"] = "Data from worker " + String(WORKER_ID);
  doc["timestamp"] = millis();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.config(staticIP, clusterGateway, subnet);
  WiFi.begin(CLUSTER_SSID, CLUSTER_PASSWORD);

  Serial.printf("Worker %d connecting to cluster network '%s'...\n", WORKER_ID, CLUSTER_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWorker %d online at %s\n", WORKER_ID, WiFi.localIP().toString().c_str());

  delay(1000);  // let the network stack stabilize

  startTime = millis();

  server.on("/", handleRoot);
  server.on("/health", handleHealth);
  server.on("/api/data", handleData);

  server.begin();
  Serial.printf("Worker %d server started on port %d\n", WORKER_ID, WORKER_PORT);
}

void loop() {
  server.handleClient();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Lost cluster connection, reconnecting...");
    WiFi.begin(CLUSTER_SSID, CLUSTER_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
    Serial.println("Reconnected to cluster.");
  }
  delay(10);
}