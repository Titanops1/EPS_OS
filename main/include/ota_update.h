#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>

#define OTA_JSON_URL "https://raw.githubusercontent.com/Titanops1/EPS_OS/refs/heads/main/latest.json"
#define OTA_FILE_PATH "/spiffs/firmware.bin"

bool ota_check_for_update(void);
bool ota_download_firmware(const char *url);
bool ota_verify_checksum(const char *expected_hash);
bool ota_perform_update(void);
void check_and_update_firmware(void);
#endif