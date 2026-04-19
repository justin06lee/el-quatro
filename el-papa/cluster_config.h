#ifndef CLUSTER_CONFIG_H
#define CLUSTER_CONFIG_H

// Your home Wi-Fi
#define WIFI_SSID     "<WIFI NAME>"
#define WIFI_PASSWORD "<WIFI PASSWORD>"

// Static IPs for each node (pick IPs outside your router's DHCP range)
// Check your router — DHCP usually hands out 192.168.1.100+
// so we'll use low numbers
#define GATEWAY_IP     192, 168, 1, 40
#define WORKER_1_IP    192, 168, 1, 41
#define WORKER_2_IP    192, 168, 1, 42
#define WORKER_3_IP    192, 168, 1, 43

#define GATEWAY_PORT   80
#define WORKER_PORT    80

// Health check interval in milliseconds
#define HEALTH_CHECK_INTERVAL 5000

#endif
