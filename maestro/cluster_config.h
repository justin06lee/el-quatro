#ifndef CLUSTER_CONFIG_H
#define CLUSTER_CONFIG_H

// External Wi-Fi
#define HOME_WIFI_SSID       "<WIFI NAME>"
// For university Wi-Fi, uncomment these and comment out HOME_WIFI_PASSWORD:
#define EAP_IDENTITY       "<EMAIL>"
#define EAP_PASSWORD       "<PASSWORD>"

// Groq API
#define GROQ_API_KEY   "<API KEY>"
#define GROQ_MODEL     "openai/gpt-oss-120b"

// Cluster network
#define CLUSTER_SSID         "EL QUATRO"
#define CLUSTER_PASSWORD     "elquatroesp"
#define CLUSTER_CHANNEL      6

// IPs
#define GATEWAY_CLUSTER_IP   192, 168, 4, 1
#define WORKER_1_IP          192, 168, 4, 11
#define WORKER_2_IP          192, 168, 4, 12
#define WORKER_3_IP          192, 168, 4, 13
#define CLUSTER_SUBNET       255, 255, 255, 0

#define GATEWAY_PORT   80
#define WORKER_PORT    80
#define HEALTH_CHECK_INTERVAL 5000

#endif
