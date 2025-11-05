#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"

#include "esp_console.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include <stdio.h>
#include <stdint.h>

#define MAX_WINDOW_PRIORITY		25

void window_manager_init(void);
bool focus_request(int app_id);
bool focus_has(int app_id);
void focus_release(int app_id);

void window_set_priority(int app_id, uint8_t priority);
void window_inc_priority(int app_id);
void window_dec_priority(int app_id);

int window_register(const char *name, TaskHandle_t task, void (*on_focus_gained)(void), void (*on_focus_lost)(void));
void window_unregister(int id) ;