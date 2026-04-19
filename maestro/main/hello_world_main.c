/* Master Node (Node 0) for ESP32-C6 */
#include "driver/temperature_sensor.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLINK_GPIO 8
#define WIFI_CHANNEL 1
#define MAX_PEERS 20
#define PACKET_THRESHOLD_PER_SEC 10

static const char *TAG = "MASTER_NODE";

// Global Handlers
led_strip_handle_t led_strip;
temperature_sensor_handle_t temp_sensor = NULL;
QueueHandle_t espnow_queue;

typedef struct {
  uint8_t type;
  uint8_t data;
} payload_t;

typedef struct {
  uint8_t mac[ESP_NOW_ETH_ALEN];
  uint8_t data[250];
  int data_len;
} espnow_event_t;

typedef struct {
  uint8_t mac[ESP_NOW_ETH_ALEN];
  uint32_t msg_count;
  TickType_t period_start;
  bool blacklisted;
  int8_t last_rssi;
} peer_record_t;

peer_record_t peers[MAX_PEERS];

// Commands
#define CMD_POTATO 1
#define CMD_REBOOT_BOOTLOADER 2

// HTML Dashboard
const char *dashboard_html =
    "<!DOCTYPE html><html><head><meta name=\"viewport\" "
    "content=\"width=device-width, initial-scale=1\">"
    "<style>body{font-family:sans-serif; background:#1e1e1e; color:#fff; "
    "text-align:center;}"
    ".card{background:#333; padding:20px; margin:10px; border-radius:10px;}"
    "button{padding:10px; margin:5px; border-radius:5px; border:none; "
    "cursor:pointer;"
    "background:#007BFF; color:white; font-size:16px;}</style></head>"
    "<body><h1>C6 Cluster Dashboard</h1>"
    "<div class=\"card\"><h3>System Health</h3>"
    "<p id=\"temp\">Temp: Loading...</p><p id=\"heap\">Heap: "
    "Loading...</p></div>"
    "<div class=\"card\"><h3>Actions</h3>"
    "<button onclick=\"fetch('/api/potato')\">Hot Potato!</button>"
    "<button style=\"background:#dc3545;\" "
    "onclick=\"fetch('/api/reboot')\">Reboot Sub-Nodes</button></div>"
    "<script>"
    "setInterval(()=>{"
    "fetch('/api/health').then(r=>r.json()).then(d=>{"
    "document.getElementById('temp').innerText='Temp: '+d.temp.toFixed(1)+' C';"
    "document.getElementById('heap').innerText='Free Heap: "
    "'+(d.heap/1024).toFixed(1)+' KB';"
    "})}, 2000);"
    "</script></body></html>";

// ==========================================
// LED Helper
// ==========================================
void set_led_color(uint8_t r, uint8_t g, uint8_t b) {
  led_strip_set_pixel(led_strip, 0, r, g, b);
  led_strip_refresh(led_strip);
}

// ==========================================
// ESP-NOW Logic
// ==========================================
void espnow_recv_cb(const esp_now_recv_info_t *esp_now_info,
                    const uint8_t *data, int data_len) {
  if (esp_now_info == NULL || esp_now_info->src_addr == NULL || data_len == 0)
    return;

  espnow_event_t evt;
  memcpy(evt.mac, esp_now_info->src_addr, ESP_NOW_ETH_ALEN);
  evt.data_len = data_len;
  if (data_len > 250)
    evt.data_len = 250;
  memcpy(evt.data, data, evt.data_len);

  // Quick enqueue (offload from interrupt/stack)
  if (xQueueSendFromISR(espnow_queue, &evt, NULL) != pdTRUE) {
    // Queue full
  }
}

void espnow_task(void *pvParameter) {
  espnow_event_t evt;
  while (1) {
    if (xQueueReceive(espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
      TickType_t now = xTaskGetTickCount();
      bool found = false;
      int free_slot = -1;

      // Watchdog Logic
      for (int i = 0; i < MAX_PEERS; i++) {
        if (!found && memcmp(peers[i].mac, evt.mac, ESP_NOW_ETH_ALEN) == 0) {
          found = true;
          if (peers[i].blacklisted)
            break;

          if (pdTICKS_TO_MS(now - peers[i].period_start) > 1000) {
            peers[i].msg_count = 0;
            peers[i].period_start = now;
          }

          peers[i].msg_count++;
          if (peers[i].msg_count > PACKET_THRESHOLD_PER_SEC) {
            peers[i].blacklisted = true;
            ESP_LOGW(TAG, "Blacklisted MAC %02x:%02x:%02x:%02x:%02x:%02x",
                     evt.mac[0], evt.mac[1], evt.mac[2], evt.mac[3], evt.mac[4],
                     evt.mac[5]);
            break;
          }

          // Process Payload (if not blacklisted)
          if (evt.data_len == sizeof(payload_t)) {
            payload_t *p = (payload_t *)evt.data;
            if (p->type == CMD_POTATO) {
              ESP_LOGI(TAG, "POTATO RECEIVED!");
              set_led_color(255, 100, 0); // Orange
              vTaskDelay(pdMS_TO_TICKS(200));
              set_led_color(0, 0, 0);
            }
          }
          break;
        }
        if (free_slot == -1 && peers[i].mac[0] == 0)
          free_slot = i; // simple check
      }

      // Add new MAC to watchlist mapping
      if (!found && free_slot != -1) {
        memcpy(peers[free_slot].mac, evt.mac, ESP_NOW_ETH_ALEN);
        peers[free_slot].period_start = now;
        peers[free_slot].msg_count = 1;
        peers[free_slot].blacklisted = false;
      }
    }
  }
}

// ==========================================
// HTTP Server Handlers
// ==========================================
static esp_err_t root_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, dashboard_html, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t health_get_handler(httpd_req_t *req) {
  float temp_val = 0;
  if (temp_sensor) {
    temperature_sensor_get_celsius(temp_sensor, &temp_val);
  }
  uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);

  char resp_str[100];
  snprintf(resp_str, sizeof(resp_str), "{\"temp\": %.1f, \"heap\": %lu}",
           temp_val, free_heap);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t potato_get_handler(httpd_req_t *req) {
  payload_t p = {.type = CMD_POTATO, .data = 1};
  uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF,
                                             0xFF, 0xFF, 0xFF}; // broadcast
  esp_now_send(broadcast_mac, (uint8_t *)&p, sizeof(p));
  set_led_color(255, 100, 0); // flash locally to show action
  vTaskDelay(pdMS_TO_TICKS(100));
  set_led_color(0, 0, 0);

  httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t reboot_get_handler(httpd_req_t *req) {
  payload_t p = {.type = CMD_REBOOT_BOOTLOADER, .data = 1};
  uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF,
                                             0xFF, 0xFF, 0xFF};
  esp_now_send(broadcast_mac, (uint8_t *)&p, sizeof(p));

  httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// ==========================================
// Initialization
// ==========================================
void init_nvs() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
}

void init_wifi() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
  (void)ap_netif; // avoid warning
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  (void)sta_netif; // avoid warning

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t ap_config = {
      .ap =
          {
              .ssid = "EL_QUATRO",
              .ssid_len = strlen("EL_QUATRO"),
              .channel = WIFI_CHANNEL,
              .password = "", // open network MVP
              .max_connection = 4,
              .authmode = WIFI_AUTH_OPEN,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

void init_espnow() {
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

  // Register broadcast peer
  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  memset(peerInfo.peer_addr, 0xFF, ESP_NOW_ETH_ALEN);
  ESP_ERROR_CHECK(esp_now_add_peer(&peerInfo));
}

void init_hardware() {
  // LED
  led_strip_config_t strip_config = {
      .strip_gpio_num = BLINK_GPIO,
      .max_leds = 1,
  };
  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000,
      .flags.with_dma = false,
  };
  ESP_ERROR_CHECK(
      led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  led_strip_clear(led_strip);

  // Temperature sensor
  temperature_sensor_config_t temp_sensor_config =
      TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  ESP_ERROR_CHECK(
      temperature_sensor_install(&temp_sensor_config, &temp_sensor));
  ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));
}

void start_webserver() {
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t uri_root = {.uri = "/",
                            .method = HTTP_GET,
                            .handler = root_get_handler,
                            .user_ctx = NULL};
    httpd_register_uri_handler(server, &uri_root);

    httpd_uri_t uri_health = {.uri = "/api/health",
                              .method = HTTP_GET,
                              .handler = health_get_handler,
                              .user_ctx = NULL};
    httpd_register_uri_handler(server, &uri_health);

    httpd_uri_t uri_potato = {.uri = "/api/potato",
                              .method = HTTP_GET,
                              .handler = potato_get_handler,
                              .user_ctx = NULL};
    httpd_register_uri_handler(server, &uri_potato);

    httpd_uri_t uri_reboot = {.uri = "/api/reboot",
                              .method = HTTP_GET,
                              .handler = reboot_get_handler,
                              .user_ctx = NULL};
    httpd_register_uri_handler(server, &uri_reboot);
  }
}

void app_main(void) {
  memset(peers, 0, sizeof(peers));
  espnow_queue = xQueueCreate(20, sizeof(espnow_event_t));

  init_nvs();
  init_hardware();
  init_wifi();
  init_espnow();
  start_webserver();

  xTaskCreate(espnow_task, "espnow_task", 4096, NULL, 4, NULL);

  ESP_LOGI(TAG, "Master Node initialized! Connect to Wi-Fi: EL_QUATRO");
}
