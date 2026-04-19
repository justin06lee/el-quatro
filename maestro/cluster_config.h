#ifndef CLUSTER_CONFIG_H
#define CLUSTER_CONFIG_H

// University Wi-Fi (WPA2-Enterprise)
#define HOME_WIFI_SSID       "eduroam"
#define EAP_IDENTITY         "jlee1121@ucsc.edu"
#define EAP_PASSWORD         "Huic3hx48xmyun^^"

// Internal cluster network
#define CLUSTER_SSID         "EL QUATRO"
#define CLUSTER_PASSWORD     "jason123456"
#define CLUSTER_CHANNEL      6

// Internal cluster IPs
#define GATEWAY_CLUSTER_IP   192, 168, 4, 1
#define WORKER_1_IP          192, 168, 4, 11
#define WORKER_2_IP          192, 168, 4, 12
#define WORKER_3_IP          192, 168, 4, 13

#define CLUSTER_SUBNET       255, 255, 255, 0

#define GATEWAY_PORT   80
#define WORKER_PORT    80

#define HEALTH_CHECK_INTERVAL 5000

#endif