#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "wifi.h"
#include "../../register_def.h"
#include "uart_lib.h"

#include "esp_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "wifi_mqtt";
#define WIFI_FILE "/spiffs/wifi/wifi.conf"
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

typedef struct {
	uint8_t wifi_reconnect;
	uint16_t ap_count;
	wifi_ap_record_t ap_info[CONFIG_ESP_WIFI_MAX_AP];
} wifi_handle_t;

wifi_handle_t wifi_handle;

static int s_retry_num = 0;

uint8_t extractInt8(const char *str) {
	const char *ptr = str; // Zeiger auf den Anfang des Strings
	uint8_t number = 0;

	// Suchen nach '=' im String
	while (*ptr != '\0') {
		if (*ptr == '=') {
			// Wenn '=' gefunden wurde, versuchen Sie, die Zahl zu konvertieren
			number = atoi(ptr + 1); // atoi wandelt den nachfolgenden Text in eine Ganzzahl um
			break; // Brechen Sie die Schleife ab, sobald die Zahl gefunden wurde
		}
		ptr++;
	}

	return number;
}

uint16_t extractInt16(const char *str) {
	const char *ptr = str; // Zeiger auf den Anfang des Strings
	uint16_t number = 0;

	// Suchen nach '=' im String
	while (*ptr != '\0') {
		if (*ptr == '=') {
			// Wenn '=' gefunden wurde, versuchen Sie, die Zahl zu konvertieren
			number = atoi(ptr + 1); // atoi wandelt den nachfolgenden Text in eine Ganzzahl um
			break; // Brechen Sie die Schleife ab, sobald die Zahl gefunden wurde
		}
		ptr++;
	}

	return number;
}

double extractDouble(const char *str) {
	const char *ptr = str; // Zeiger auf den Anfang des Strings
	double number = 0.0;
	// Suchen nach '=' im String
	while (*ptr != '\0') {
		if (*ptr == '=') {
			// Wenn '=' gefunden wurde, versuchen Sie, die Zahl zu konvertieren
			number = strtod(ptr + 1, NULL); // strtod wandelt den nachfolgenden Text in eine Dezimalzahl um
			break; // Brechen Sie die Schleife ab, sobald die Zahl gefunden wurde
		}
		ptr++;
	}
	return number;
}


static void log_error_if_nonzero(const char *message, int error_code)
{
	if (error_code != 0) {
		ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
	}
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
	ESP_LOGD(TAG, "[MQTT] Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
	esp_mqtt_event_handle_t event = event_data;
	esp_mqtt_client_handle_t client = event->client;
	int msg_id;
	char topic[255];
	switch ((esp_mqtt_event_id_t)event_id) {
	case MQTT_EVENT_CONNECTED:
		ESP_LOGI(TAG, "[MQTT] MQTT_EVENT_CONNECTED");
		// msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
		// ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

		msg_id = esp_mqtt_client_subscribe(client, "#", 0);
		ESP_LOGI(TAG, "[MQTT] sent subscribe successful, msg_id=%d", msg_id);

		// msg_id = esp_mqtt_client_unsubscribe(client, "keepAlive");
		// ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
		break;
	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "[MQTT] MQTT_EVENT_DISCONNECTED");
		break;

	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGI(TAG, "[MQTT] MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
		// msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
		// ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		ESP_LOGI(TAG, "[MQTT] MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGI(TAG, "[MQTT] MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_DATA:
		/***********************************************
		 * Output UART ESP_LOGI
		 * I (9984) UDOO_KEY_ESP32: [MQTT] MQTT_EVENT_DATA
		 * I (9984) UDOO_KEY_ESP32: [MQTT] TOPIC=can/temperature/server
		 * I (9994) UDOO_KEY_ESP32: [MQTT] DATA=53
		*/
		// ESP_LOGD(TAG, "[MQTT] MQTT_EVENT_DATA");
		// ESP_LOGD(TAG, "[MQTT] TOPIC=%.*s\r\n", event->topic_len, event->topic);
		// ESP_LOGD(TAG, "[MQTT] DATA=%.*s\r\n", event->data_len, event->data);

		//Parse Topic to String so we can compare it
		for(uint8_t i = 0; i < event->topic_len; i++) {
			topic[i] = event->topic[i];
		}
		topic[event->topic_len] = '\0';

		//compare Topic
		if(strcmp(topic, "desktop/distance") == 0)
		{ //String Identisch
			uint8_t buffer[1];
			buffer[0] = atoi(event->data);
			sendRPi(REG_DIST, &buffer, 1);
		}
		else if(strcmp(topic, "can/light") == 0)
		{ //String Identisch
			uint8_t buffer[1];
			buffer[0] = atoi(event->data);
			sendRPi(REG_LIGHT, &buffer, 1);
		}
		// else if(strcmp(topic, "keepAlive") == 0)
		// { //String Identisch
		// 	//uint8_t buffer[1];
		// 	//buffer[0] = atoi(event->data);
		// 	sendRPi(2, &event->data, event->data_len);
		// }
		break;
	case MQTT_EVENT_ERROR:
		ESP_LOGI(TAG, "[MQTT] MQTT_EVENT_ERROR");
		if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
			log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
			log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
			log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
			ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

		}
		break;
	default:
		ESP_LOGI(TAG, "[MQTT] Other event id:%d", event->event_id);
		break;
	}
}

void mqtt_app_start(void)
{
	const esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = MQTT_BROKER_URI,
		//.username = MQTT_USERNAME,
		//.password = MQTT_PASSWORD,
		//.event_handle = mqtt_event_handler_cb,
	};
	esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
	esp_mqtt_client_start(client);
}

static void event_handler(void* arg, esp_event_base_t event_base,
								int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED && wifi_handle.wifi_reconnect == 1) {
		if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "[WiFi] retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
			xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
		}
		ESP_LOGI(TAG,"[WiFi] connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGD(TAG, "[WiFi] got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
		xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
	}
}

void print_ip_address() {
	esp_netif_ip_info_t ip_info;
	
	// Hole die IP-Informationen der STA-Schnittstelle
	if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info) == ESP_OK) {
		printf("IP-Adresse: " IPSTR "\n", IP2STR(&ip_info.ip));
		printf("Gateway:    " IPSTR "\n", IP2STR(&ip_info.gw));
		printf("Netzmaske:  " IPSTR "\n", IP2STR(&ip_info.netmask));
	} else {
		ESP_LOGE("NETWORK", "Konnte keine IP-Adresse abrufen.");
	}
}

int wifi_scan() {

	esp_wifi_scan_start(NULL, true);
	esp_wifi_scan_get_ap_num(&wifi_handle.ap_count);
	if(wifi_handle.ap_count == 0) {
		return -1;
	}else if (wifi_handle.ap_count > CONFIG_ESP_WIFI_MAX_AP) {
		wifi_handle.ap_count = CONFIG_ESP_WIFI_MAX_AP;
	}
	esp_wifi_scan_get_ap_records(&wifi_handle.ap_count, wifi_handle.ap_info);
	return wifi_handle.ap_count;
}

void print_wifi_scan() {
	printf("Gefundene Netzwerke: %d\n", wifi_handle.ap_count);
	for(uint8_t i = 0; i < wifi_handle.ap_count; i++) {
		printf("SSID: %s, RSSI: %d\n", wifi_handle.ap_info[i].ssid, wifi_handle.ap_info[i].rssi);
	}
}

void save_wifi_credentials(const char *ssid, const char *password) {

	FILE *file = fopen(WIFI_FILE, "r");
	if (!file) {
		ESP_LOGD("SPIFFS", "WLAN-Datei nicht gefunden, erstelle neue Datei...");
		file = fopen(WIFI_FILE, "w");
	}
	
	char line[128];
	int found = 0;
	char new_data[1024] = "";
	
	ESP_LOGD("SPIFFS", "Speichere WLAN-Daten...");
	while (fgets(line, sizeof(line), file)) {
		char stored_ssid[32], stored_pass[64];
		sscanf(line, "%31[^:]:%63s", stored_ssid, stored_pass);
		if (strcmp(stored_ssid, ssid) == 0) {
			snprintf(line, sizeof(line), "%s:%s\n", ssid, password);
			found = 1;
		}
		strncat(new_data, line, sizeof(new_data) - strlen(new_data) - 1);
	}
	fclose(file);
	
	//WICHTIG die Datei muss immer wieder neu überschrieben werden, sonst wird die liste immer länger.
	//Dadurch das die Datei immer wieder neu geschrieben wird, wird die Liste immer wieder neu erstellt.
	file = fopen(WIFI_FILE, "w");
	if (!found) {
		//Schreibt neue Daten in die Datei
		fprintf(file, "%s:%s\n", ssid, password);
	}
	//Schreibt die alten Daten in die Datei
	fputs(new_data, file);
	fclose(file);
	ESP_LOGD("SPIFFS", "WLAN-Daten gespeichert!");
}

int load_wifi_credentials(char *ssid, char *password) {
	char line[128];
	uint8_t ret = 0;
	FILE *file = fopen(WIFI_FILE, "r");
	if (file == NULL) {
		ESP_LOGE("SPIFFS", "Keine WLAN-Datei gefunden!");
		return -1;
	}
	while (fgets(line, sizeof(line), file)) {
		char stored_ssid[32], stored_pass[64];
		sscanf(line, "%31[^:]:%63s", stored_ssid, stored_pass);
		if (strcmp(stored_ssid, ssid) == 0) {
			strcpy(password, stored_pass);
			ret = 1;
		}
	}
	fclose(file);
	ESP_LOGD("SPIFFS", "Geladene WLAN-Daten: SSID=%s", ssid);
	return ret;
}

void list_wifi_credentials() {
	char line[128];

	FILE *file = fopen(WIFI_FILE, "r");
	if (file == NULL) {
		ESP_LOGE("SPIFFS", "Keine WLAN-Datei gefunden!");
		return;
	}
	while (fgets(line, sizeof(line), file)) {
		char stored_ssid[32], stored_pass[64];
		sscanf(line, "%31[^:]:%63s", stored_ssid, stored_pass);
		snprintf(line, sizeof(line), "%s:%s\n", stored_ssid, stored_pass);
		printf("%s", line);
	}
	fclose(file);
}

void reset_wifi_credentials(const char *ssid) {
	char line[128];
	char new_data[1024] = "";
	FILE *file = fopen(WIFI_FILE, "r");
	if (file == NULL) {
		ESP_LOGE("SPIFFS", "Keine WLAN-Datei gefunden!");
		return;
	}
	while (fgets(line, sizeof(line), file)) {
		char stored_ssid[32], stored_pass[64];
		sscanf(line, "%31[^:]:%63s", stored_ssid, stored_pass);
		if (strcmp(stored_ssid, ssid) != 0) {
			strncat(new_data, line, sizeof(new_data) - strlen(new_data) - 1);
		}
	}
	fclose(file);
	//WICHTIG die Datei muss immer wieder neu überschrieben werden, sonst wird die liste immer länger.
	//Dadurch das die Datei immer wieder neu geschrieben wird, wird die Liste immer wieder neu erstellt.
	file = fopen(WIFI_FILE, "w");
	fputs(new_data, file);
	fclose(file);
	ESP_LOGD("SPIFFS", "WLAN-Daten: SSID=%s gelöscht!", ssid);
}

void delete_wifi_config() {
	remove(WIFI_FILE);
}

void wlan_disconnect(void)
{
	wifi_handle.wifi_reconnect = 0;
	esp_wifi_disconnect();
}

void connect_saved_wifi() {
	char *password = heap_caps_malloc(64, MALLOC_CAP_SPIRAM);;
	EventBits_t bits;
	int ret;

	wifi_scan();

	for(uint8_t i = 0; i < wifi_handle.ap_count; i++) {
		ret = load_wifi_credentials((char *)wifi_handle.ap_info[i].ssid, password);
		if(ret == 1) {
			ESP_LOGI(TAG, "[WiFi] Verbinde mit SSID:%s", wifi_handle.ap_info[i].ssid);
			bits = wlan_connect((char *)wifi_handle.ap_info[i].ssid, password);
			if (bits & WIFI_CONNECTED_BIT) {
				heap_caps_free(password);
				return;
			}
		}
	}
	heap_caps_free(password);
}

EventBits_t wlan_connect(const char *ssid, const char *password)
{
	wifi_config_t wifi_config = {
		.sta = {
			.ssid = "",
			.password = "",
		},
	};
	strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid)-1);
	strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password)-1);
	esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
	wifi_handle.wifi_reconnect = 1;
	esp_wifi_connect();
	vTaskDelay(pdMS_TO_TICKS(5000)); // Warte 5 Sek. auf Verbindung
	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
		WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
		pdFALSE,
		pdFALSE,
		portMAX_DELAY);

	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "[WiFi] connected to ap SSID:%s", ssid);
		obtain_time();
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "[WiFi] Failed to connect to SSID:%s, password:%s",
				 ssid, password);
	} else {
		ESP_LOGE(TAG, "[WiFi] UNEXPECTED EVENT");
	}
	return bits;
}

void start_wifi() {
	ESP_LOGI("WIFI", "WLAN-Modul wird gestartet...");
	wifi_config_t wifi_config = {
		.sta = {
			.ssid = "",
			.password = "",
			/* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
			 * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
			 * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
		 * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
			 */
			.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
			.sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
		},
	};
	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
	esp_wifi_start();
}

void stop_wifi() {
	ESP_LOGI("WIFI", "WLAN-Modul wird gestoppt...");
	esp_wifi_stop();
}

void restart_wifi() {
	ESP_LOGI("WIFI", "WLAN-Modul wird neu gestartet...");
	esp_wifi_stop();   // WLAN-Modul stoppen
	vTaskDelay(pdMS_TO_TICKS(1000));
	esp_wifi_start();  // WLAN-Modul neu starten
}

void wifi_init_sta(void)
{
	s_wifi_event_group = xEventGroupCreate();
	esp_netif_init();

	esp_event_loop_create_default();
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	esp_event_handler_instance_register(WIFI_EVENT,
										ESP_EVENT_ANY_ID,
										&event_handler,
										NULL,
										&instance_any_id);
	esp_event_handler_instance_register(IP_EVENT,
										IP_EVENT_STA_GOT_IP,
										&event_handler,
										NULL,
										&instance_got_ip);

	start_wifi();
	connect_saved_wifi();
	wifi_handle.wifi_reconnect = 1;
	ESP_LOGI(TAG, "[WiFi] wifi_init_sta finished.");
}

void obtain_time(void) {
    printf("Synchronizing time with NTP...\n");

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");  // Standard-NTP-Server
    sntp_init();

    // Warten, bis Zeit synchronisiert ist
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int max_retries = 15;

    while (timeinfo.tm_year < (2020 - 1900) && ++retry < max_retries) {
        printf("Waiting for system time to be set... (%d/%d)\n", retry, max_retries);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (retry >= max_retries) {
        printf("Failed to get NTP time.\n");
    } else {
        printf("Time synchronized: %s", asctime(&timeinfo));
    }
}