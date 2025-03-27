#ifndef WIFI_H
#define WIFI_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <driver/gpio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "mqtt_client.h"

// MQTT-Broker-Einstellungen
#define MQTT_BROKER_URI "mqtt://192.168.2.200"
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_TOPIC "keepAlive" // Ändere auf das gewünschte Thema

// WiFi-Konfiguration
#define CONFIG_ESP_WIFI_MAX_AP 20
#define CONFIG_ESP_MAXIMUM_RETRY	10
#define CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK		1

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT     BIT0
#define WIFI_FAIL_BIT          BIT1
#define WIFI_DISCONNECTED_BIT  BIT2
#define WIFI_SCAN_DONE_BIT     BIT3
#define WIFI_AUTH_FAIL_BIT     BIT4
#define WIFI_IP_OBTAINED_BIT   BIT5

void mqtt_app_start(void);
void print_ip_address(void);
int wifi_scan();
void print_wifi_scan();
void save_wifi_credentials(const char *ssid, const char *password);
int load_wifi_credentials(char *ssid, char *password);
void list_wifi_credentials();
void reset_wifi_credentials(const char *ssid);
void delete_wifi_config();
void wlan_disconnect(void);
void connect_saved_wifi();
EventBits_t wlan_connect(const char *ssid, const char *password);
void start_wifi();
void stop_wifi();
void restart_wifi();
void wifi_init_sta(void);

#endif