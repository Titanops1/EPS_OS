#include "ota_update.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
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
uint32_t file_downlod_size = 0;
int64_t start_time = 0;
static int g_firmware_content_length = -1;

#define PROGRESS_WIDTH 40

void print_progress_bar(int current, int total) {
    int progress = (current * PROGRESS_WIDTH) / total;
    printf("\r[");
    for (int i = 0; i < PROGRESS_WIDTH; i++) {
        if (i < progress)
            printf("#");
        else
            printf(" ");
    }
    printf("] %3d %%", (current * 100) / total);
    fflush(stdout);
}

void remove_whitespace(char *str) {
	char *src = str, *dst = str;
	while (*src) {
		if (*src != '\n' && *src != '\t' && *src != '\r') {
			*dst++ = *src;
		}
		src++;
	}
	*dst = '\0';
}

static esp_err_t _http_event_handler_version(esp_http_client_event_t *evt) {
	switch (evt->event_id) {
		case HTTP_EVENT_ON_DATA:
			//printf("HTTP_EVENT_ON_DATA, len=%d\n", evt->data_len);
			//printf("Empfangene Daten: %.*s\n", evt->data_len, (char*)evt->data);
			if (!esp_http_client_is_chunked_response(evt->client)) {
				if (response_len + evt->data_len < sizeof(response_buffer) - 1) {
					memcpy(response_buffer + response_len, evt->data, evt->data_len);
					response_len += evt->data_len;
					response_buffer[response_len] = '\0';  // String terminieren
				}
			}
			break;
		default:
			//printf("Unbekanntes Ereignis: %d\n", evt->event_id);
			break;
	}
	return ESP_OK;
}

FILE *file = NULL;
static esp_err_t _http_event_handler_firmware(esp_http_client_event_t *evt) {
	switch (evt->event_id) {
		case HTTP_EVENT_ERROR:
			if (file) {
				fclose(file);
				file = NULL;
			}
			printf("HTTP_EVENT_ERROR\n");
			break;
		case HTTP_EVENT_ON_CONNECTED:
			//printf("HTTP_EVENT_ON_CONNECTED\n");
			file = fopen(OTA_FILE_PATH, "wb");
			file_downlod_size = 0;

			// int content_length = esp_http_client_fetch_headers(evt->client);
			// printf("Gesamtgröße der Firmware: %d Bytes\n", content_length);

			start_time = esp_timer_get_time();  // Startzeit in µs
			break;
		case HTTP_EVENT_ON_DATA:
			if (!file) {
				fclose(file);
				file = NULL;
				//printf("HTTP_EVENT_ON_DATA: Datei nicht geöffnet\n");
				return ESP_FAIL;
			}
			if (evt->data_len > 0) {
				fwrite(evt->data, 1, evt->data_len, file);
				file_downlod_size += evt->data_len;
				print_progress_bar(file_downlod_size, g_firmware_content_length);
				//printf("File download, size=%ld Byte\n", file_downlod_size);
				//printf("HTTP_EVENT_ON_DATA, len=%d\n", evt->data_len);
				//printf("Empfangene Daten: %.*s\n", evt->data_len, (char*)evt->data);
			}
			break;
		case HTTP_EVENT_ON_FINISH:
			//printf("HTTP_EVENT_ON_FINISH\n");
			if (file) {
				fclose(file);
				file = NULL;
			}

			int64_t end_time = esp_timer_get_time();  // in µs
			int64_t duration_us = end_time - start_time;
			float duration_sec = duration_us / 1000000.0;

			if (duration_sec > 0) {
				float speed_kbps = (file_downlod_size / 1024.0) / duration_sec;
				printf("\nDownload abgeschlossen: %lu Bytes in %.2f Sekunden\n", file_downlod_size, duration_sec);
				printf("Durchschnittliche Geschwindigkeit: %.2f KB/s\n", speed_kbps);
			} else {
				printf("Download abgeschlossen. Zeitmessung zu kurz oder fehlgeschlagen.\n");
			}
			break;
		case HTTP_EVENT_DISCONNECTED:
			//printf("HTTP_EVENT_DISCONNECTED\n");
			if (file) {
				fclose(file);
				file = NULL;
			}
			break;
		case HTTP_EVENT_ON_HEADER:
			if (strcasecmp(evt->header_key, "Content-Length") == 0) {
				g_firmware_content_length = atoi(evt->header_value);
				printf("Firmwaregröße: %d Bytes\n", g_firmware_content_length);
			}
			break;
		default:
			//printf("Unbekanntes Ereignis: %d\n", evt->event_id);
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
		if (ota_state == ESP_OTA_IMG_NEW) {
			ESP_LOGI("OTA_STATE", "Neu installiert und noch nicht bestätigt.\n");
		} else if (ota_state == ESP_OTA_IMG_VALID) {
			ESP_LOGI("OTA_STATE", "Läuft normal.\n");
		} else if (ota_state == ESP_OTA_IMG_INVALID) {
			ESP_LOGI("OTA_STATE", "Fehlgeschlagen, Rollback wird ausgeführt.");
			esp_ota_mark_app_invalid_rollback_and_reboot();
		} else if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
			ESP_LOGI("OTA_STATE", "OTA-Status: Pending verify → Versuche zu bestätigen...");
			if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
				ESP_LOGI("OTA_STATE", "Firmware erfolgreich als gültig markiert!");
			} else {
				ESP_LOGE("OTA_STATE", "Fehler beim Bestätigen der Firmware! Rollback wird ausgelöst.");
				esp_ota_mark_app_invalid_rollback_and_reboot();
			}
		} else {
			ESP_LOGI("OTA_STATE", "Unbekannter Zustand. %d", ota_state);
		}
	} else {
		ESP_LOGE("OTA_STATE", "Konnte OTA-Status nicht auslesen.");
	}
}

/**
 * Lädt die JSON-Datei von GitHub und überprüft die Version
 */
bool ota_check_for_update(void) {
	esp_http_client_config_t config = {
		.url = OTA_JSON_URL,
		.cert_pem = ca_cert_start,  // CA-Zertifikat
		//.skip_cert_common_name_check = true,
		.timeout_ms = 20000,
		.transport_type = HTTP_TRANSPORT_OVER_SSL,  // HTTPS
		.event_handler = _http_event_handler_version,
	};

	esp_http_client_handle_t client = esp_http_client_init(&config);
	esp_err_t err = esp_http_client_perform(client);

	if (err == ESP_OK) {
		int status_code = esp_http_client_get_status_code(client);
		int content_length = esp_http_client_get_content_length(client);
		if (status_code == 200 && content_length > 0) {
			printf("HTTP Status: %d\n", status_code);
			//printf("Inhalt der Antwort: %s\n", response_buffer);
			remove_whitespace(response_buffer);
			//printf("Inhalt ohne Leerzeichen: %s\n", response_buffer);
		} else if (status_code == 404) {
			printf("Fehler: Datei nicht gefunden (404)\n");
		} else if (status_code == 500) {
			printf("Fehler: Serverfehler (500)\n");
		} else if (status_code == 403) {
			printf("Fehler: Zugriff verweigert (403)\n");
		} else {
			printf("HTTP Fehler: Statuscode %d\n", status_code);
		}
	} else {
		printf("Fehler bei der Anfrage: %s\n", esp_err_to_name(err));
	}
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

	printf("Aktuelle Version: %s\n", OS_VERSION);
	printf("Neueste Version: %s\n", latest_version);
	printf("Firmware-URL: %s\n", firmware_url);
	printf("Checksumme: %s\n", firmware_checksum);

	if(strcmp(latest_version, OS_VERSION) == 0) {
		printf("Keine neue Firmware verfügbar.\n");
		return false;
	}
	//printf("Neue Version: %s\n", latest_version);
	return true;
}

/**
 * Lädt die Firmware-Datei von GitHub herunter
 */
bool ota_download_firmware(const char *url) {
	esp_http_client_config_t config = {
		.url = url,
		.cert_pem = ca_cert_start,  // CA-Zertifikat
		//.skip_cert_common_name_check = true,
		.timeout_ms = 20000,
		.transport_type = HTTP_TRANSPORT_OVER_SSL,  // HTTPS
		.event_handler = _http_event_handler_firmware,
	};

	esp_http_client_handle_t client = esp_http_client_init(&config);
	esp_http_client_perform(client);

	if (g_firmware_content_length > 0) {
		printf("Gesamtgröße der Firmware: %d Bytes\n", g_firmware_content_length);
	} else {
		printf("Gesamtgröße der Firmware konnte nicht aus Header gelesen werden.\n");
	}

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

	esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
	if (err != ESP_OK) {
		ESP_LOGE("UPDATE_TASK", "esp_ota_begin fehlgeschlagen: %s", esp_err_to_name(err));
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
	remove(OTA_FILE_PATH);  // Firmware-Datei löschen
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
					remove(OTA_FILE_PATH);  // Firmware-Datei löschen
				}
			} else {
				printf("Fehler: Checksumme falsch! Update abgebrochen.\n");
				remove(OTA_FILE_PATH);  // Firmware-Datei löschen
			}
		} else {
			printf("Fehler beim Download!\n");
			remove(OTA_FILE_PATH);  // Firmware-Datei löschen
		}
	} else {
		printf("Keine neue Firmware gefunden.\n");
	}
}