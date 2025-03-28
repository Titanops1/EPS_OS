#include "ota_update.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "mbedtls/md.h"
#include "lwip/netdb.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include "version.h"

static char latest_version[16];
static char firmware_url[256];
static char firmware_checksum[65];

extern const char ca_cert_start[] asm("_binary_ca_cert_pem_start");
extern const char ca_cert_end[] asm("_binary_ca_cert_pem_end");

static char response_buffer[512] = {0};
static int response_len = 0;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
	switch (evt->event_id) {
		case HTTP_EVENT_ERROR:
			printf("HTTP_EVENT_ERROR\n");
			break;
		case HTTP_EVENT_ON_CONNECTED:
			printf("HTTP_EVENT_ON_CONNECTED\n");
			break;
		case HTTP_EVENT_HEADER_SENT:
			printf("HTTP_EVENT_HEADER_SENT\n");
			break;
		case HTTP_EVENT_ON_HEADER:
			printf("HTTP_EVENT_HEADERS_RECEIVED\n");
			printf("Header: %s: %s\n", evt->header_key, evt->header_value);
			break;
		case HTTP_EVENT_ON_DATA:
			printf("HTTP_EVENT_ON_DATA, len=%d\n", evt->data_len);
			printf("Empfangene Daten: %.*s\n", evt->data_len, (char*)evt->data);
			if (!esp_http_client_is_chunked_response(evt->client)) {
				if (response_len + evt->data_len < sizeof(response_buffer) - 1) {
					memcpy(response_buffer + response_len, evt->data, evt->data_len);
					response_len += evt->data_len;
					response_buffer[response_len] = '\0';  // String terminieren
				}
			}
			break;
		case HTTP_EVENT_ON_FINISH:
			printf("HTTP_EVENT_ON_FINISH\n");
			break;
		case HTTP_EVENT_DISCONNECTED:
			printf("HTTP_EVENT_DISCONNECTED\n");
			break;
		default:
			printf("Unbekanntes Ereignis: %d\n", evt->event_id);
			break;
	}
	return ESP_OK;
}

/**
 * Zeigt den aktuellen Firmware-Status an
 */
void ota_show_status(void) {
	const esp_partition_t *running = esp_ota_get_running_partition();
	esp_ota_img_states_t ota_state;

	if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
		printf("Firmware-Status: ");
		if (ota_state == ESP_OTA_IMG_NEW) {
			printf("Neu installiert und noch nicht bestätigt.\n");
		} else if (ota_state == ESP_OTA_IMG_VALID) {
			printf("Läuft normal.\n");
		} else if (ota_state == ESP_OTA_IMG_INVALID) {
			printf("Fehlgeschlagen, Rollback wird ausgeführt.\n");
			esp_ota_mark_app_invalid_rollback_and_reboot();
		} else {
			printf("Unbekannter Zustand.\n");
		}
	} else {
		printf("Konnte OTA-Status nicht abrufen.\n");
	}
}

/**
 * Lädt die JSON-Datei von GitHub und überprüft die Version
 */
bool ota_check_for_update(void) {
	char buffer[512] = {0};
	esp_http_client_config_t config = {
		.url = OTA_JSON_URL,
		.cert_pem = ca_cert_start,  // CA-Zertifikat
		//.skip_cert_common_name_check = true,
		.timeout_ms = 20000,
		.transport_type = HTTP_TRANSPORT_OVER_SSL,  // HTTPS
		.event_handler = _http_event_handler,
	};

	time_t now;
	time(&now);
	printf("Current time: %s\n", ctime(&now));

	esp_log_level_set("*", ESP_LOG_DEBUG);
	esp_http_client_handle_t client = esp_http_client_init(&config);
	esp_err_t err = esp_http_client_perform(client);

	printf("Länge des Zertifikats: %d\n", (int)(ca_cert_end - ca_cert_start));

	if (err == ESP_OK) {
		int status_code = esp_http_client_get_status_code(client);
		if (status_code == 200) {
			int content_length = esp_http_client_get_content_length(client);
			if (content_length > 0) {
				esp_err_t err = esp_http_client_fetch_headers(client);
				if (err != ESP_OK) {
					printf("Fehler beim Abrufen der Header: %s\n", esp_err_to_name(err));
				}else {
					int ret = esp_http_client_read(client, buffer, sizeof(buffer));
					if (ret > 0) {
						printf("Antwort erhalten: %s\n", buffer);
					} else {
						int status_code = esp_http_client_get_status_code(client);
						printf("Fehler beim Lesen der Antwort. Ret: %d Content %d Status: %d\n", ret, content_length, status_code);
					}
				}
			} else {
				printf("Keine Daten im Antwortkörper.\n");
			}
		} else {
			printf("HTTP Fehler: Statuscode %d\n", status_code);
		}
	} else {
		printf("Fehler bei der Anfrage: %s\n", esp_err_to_name(err));
	}
	esp_log_level_set("*", ESP_LOG_NONE);
	esp_http_client_cleanup(client);

	cJSON *json = cJSON_Parse(response_buffer);
	if (!json) {
		printf("Fehler: JSON ungültig\n");
		printf("JSON-Parsing fehlgeschlagen: %s\n", cJSON_GetErrorPtr());
		printf("JSON: %s\n", response_buffer);
		return false;
	}

	const char *version = cJSON_GetObjectItem(json, "version")->valuestring;
	const char *url = cJSON_GetObjectItem(json, "url")->valuestring;
	const char *checksum = cJSON_GetObjectItem(json, "checksum")->valuestring;

	strcpy(latest_version, version);
	strcpy(firmware_url, url);
	strcpy(firmware_checksum, checksum);

	cJSON_Delete(json);
	if(strcmp(version, OS_VERSION) == 0) {
		printf("Keine neue Firmware verfügbar.\n");
		return false;
	}
	printf("Neue Version: %s\n", latest_version);
	return true;
}

/**
 * Lädt die Firmware-Datei von GitHub herunter
 */
bool ota_download_firmware(const char *url) {
	esp_http_client_config_t config = {
		.url = url,
		.timeout_ms = 10000,
	};

	esp_http_client_handle_t client = esp_http_client_init(&config);
	esp_http_client_perform(client);

	FILE *file = fopen(OTA_FILE_PATH, "wb");
	if (!file) {
		printf("Fehler: Kann Datei nicht öffnen\n");
		return false;
	}

	char buffer[1024];
	int read_bytes;
	while ((read_bytes = esp_http_client_read(client, buffer, sizeof(buffer))) > 0) {
		fwrite(buffer, 1, read_bytes, file);
	}

	fclose(file);
	esp_http_client_cleanup(client);
	return true;
}

/**
 * Überprüft die Checksumme der heruntergeladenen Datei
 */
bool ota_verify_checksum(const char *expected_hash) {
	FILE *file = fopen(OTA_FILE_PATH, "rb");
	if (!file) return false;

	uint8_t hash[32];
	mbedtls_md_context_t ctx;
	mbedtls_md_init(&ctx);
	mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
	mbedtls_md_starts(&ctx);

	uint8_t buffer[1024];
	size_t read_bytes;
	while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		mbedtls_md_update(&ctx, buffer, read_bytes);
	}

	mbedtls_md_finish(&ctx, hash);
	mbedtls_md_free(&ctx);
	fclose(file);

	char hash_str[65];
	for (int i = 0; i < 32; i++) {
		sprintf(&hash_str[i * 2], "%02x", hash[i]);
	}

	return strcmp(hash_str, expected_hash) == 0;
}

/**
 * Führt das OTA-Update aus und führt ein Rollback durch, falls es fehlschlägt
 */
bool ota_perform_update(void) {
	const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
	esp_ota_handle_t update_handle;
	
	FILE *file = fopen(OTA_FILE_PATH, "rb");
	if (!file) {
		printf("Fehler: Kann Firmware nicht öffnen.\n");
		return false;
	}

	if (esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle) != ESP_OK) {
		fclose(file);
		printf("Fehler: OTA-Begin fehlgeschlagen.\n");
		return false;
	}

	char buffer[1024];
	int read_bytes;
	while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		if (esp_ota_write(update_handle, buffer, read_bytes) != ESP_OK) {
			fclose(file);
			esp_ota_end(update_handle);
			printf("Fehler: OTA-Schreibfehler. Rollback wird ausgeführt.\n");
			esp_ota_mark_app_invalid_rollback_and_reboot();
			return false;
		}
	}

	fclose(file);
	if (esp_ota_end(update_handle) != ESP_OK) {
		printf("Fehler: OTA-Ende fehlgeschlagen. Rollback wird ausgeführt.\n");
		esp_ota_mark_app_invalid_rollback_and_reboot();
		return false;
	}

	if (esp_ota_set_boot_partition(update_partition) != ESP_OK) {
		printf("Fehler: Boot-Partition konnte nicht gesetzt werden. Rollback wird ausgeführt.\n");
		esp_ota_mark_app_invalid_rollback_and_reboot();
		return false;
	}

	printf("Update erfolgreich! Neustart...\n");
	esp_restart();
	return true;
}

void check_and_update_firmware(void) {
	printf("Überprüfe aktuellen Firmware-Status...\n");
	ota_show_status();

	if (ota_check_for_update()) {
		printf("Firmware wird heruntergeladen...\n");
		if (ota_download_firmware(firmware_url)) {
			printf("Überprüfe Checksumme...\n");
			if (ota_verify_checksum(firmware_checksum)) {
				printf("Checksumme OK! Starte Update...\n");
				if (!ota_perform_update()) {
					printf("Update fehlgeschlagen, Rollback wird ausgeführt.\n");
				}
			} else {
				printf("Fehler: Checksumme falsch! Update abgebrochen.\n");
			}
		} else {
			printf("Fehler beim Download!\n");
		}
	} else {
		printf("Keine neue Firmware gefunden.\n");
	}
}