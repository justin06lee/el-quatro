#ifndef CLUSTER_CONFIG_H
#define CLUSTER_CONFIG_H

// EL_QUATRO private network (created by maestro AP)
#define WIFI_SSID     "EL_QUATRO"
#define WIFI_PASSWORD ""

// Static IPs on the maestro AP subnet (default ESP-IDF AP is 192.168.4.1)
#define GATEWAY_IP     192, 168, 4, 1
#define WORKER_1_IP    192, 168, 4, 41
#define WORKER_2_IP    192, 168, 4, 42
#define WORKER_3_IP    192, 168, 4, 43

#define GATEWAY_PORT   80
#define WORKER_PORT    80

// Health check interval in milliseconds
#define HEALTH_CHECK_INTERVAL 5000

#endif
