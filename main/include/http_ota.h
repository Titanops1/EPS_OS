#ifndef HTTP_OTA_H
#define HTTP_OTA_H

#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_flash_partitions.h>
#include <esp_log.h>
#include <esp_err.h>
#include "esp_app_desc.h"

#define OS_VERSION "1.0.1"

void start_webserver(void);
void stop_webserver(void);
#endif