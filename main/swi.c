#include "swi.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "swi";

typedef struct {
	bool active;
	TaskHandle_t task;          // App task handle (kann NULL sein)
	QueueHandle_t queue;        // Queue für eingehende Messages
	uint16_t queue_len;
} swi_app_entry_t;

static swi_app_entry_t app_table[SWI_MAX_APPS];
static SemaphoreHandle_t app_table_mutex = NULL;

void swi_init(void)
{
	if (app_table_mutex) return; // schon initialisiert
	app_table_mutex = xSemaphoreCreateMutex();
	memset(app_table, 0, sizeof(app_table));
	for (int i = 0; i < SWI_MAX_APPS; ++i) {
		app_table[i].active = false;
		app_table[i].task = NULL;
		app_table[i].queue = NULL;
		app_table[i].queue_len = 0;
	}
}

swi_app_id_t swi_register_app(TaskHandle_t task, uint16_t queue_len)
{
	if (!app_table_mutex) swi_init();
	if (queue_len == 0) queue_len = SWI_QUEUE_LEN;

	xSemaphoreTake(app_table_mutex, portMAX_DELAY);
	for (int i = 0; i < SWI_MAX_APPS; ++i) {
		if (!app_table[i].active) {
			app_table[i].queue = xQueueCreate(queue_len, sizeof(swi_msg_t));
			if (!app_table[i].queue) {
				xSemaphoreGive(app_table_mutex);
				ESP_LOGE(TAG, "Queue create failed for app slot %d", i);
				return -1;
			}
			app_table[i].active = true;
			app_table[i].task = task;
			app_table[i].queue_len = queue_len;
			xSemaphoreGive(app_table_mutex);
			ESP_LOGI(TAG, "Register APP with ID: %d (queue_len=%d)", i, queue_len);
			return i;
		}
	}
	xSemaphoreGive(app_table_mutex);
	return -1; // no slot
}

swi_app_id_t swi_get_appId()
{
	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	xSemaphoreTake(app_table_mutex, portMAX_DELAY);
	for (int i = 0; i < SWI_MAX_APPS; ++i) {
		if(app_table[i].task == task)
		{
			xSemaphoreGive(app_table_mutex);
			ESP_LOGI(TAG, "Registered APP found, ID: %d", i);
			return i;
		}
	}
	xSemaphoreGive(app_table_mutex);
	return -1;
}

bool swi_get_notification(uint32_t *notified, uint32_t wait_time)
{
	if (xTaskNotifyWait(0, 0xFFFFFFFF, &notified, wait_time) == pdTRUE) {
		ESP_LOGD(TAG, "Notification");
		return 1;
	}
	ESP_LOGD(TAG, "No Notification");
	return 0;
}

void swi_unregister_app(swi_app_id_t app_id)
{
	if (app_id < 0 || app_id >= SWI_MAX_APPS) return;
	xSemaphoreTake(app_table_mutex, portMAX_DELAY);
	if (app_table[app_id].active) {
		if (app_table[app_id].queue) {
			vQueueDelete(app_table[app_id].queue);
			app_table[app_id].queue = NULL;
		}
		app_table[app_id].active = false;
		app_table[app_id].task = NULL;
		app_table[app_id].queue_len = 0;
	}
	xSemaphoreGive(app_table_mutex);
	ESP_LOGI(TAG, "Unregistered APP-ID: %d", app_id);
}

static bool is_valid_app(swi_app_id_t app_id)
{
	if (app_id < 0 || app_id >= SWI_MAX_APPS) return false;
	return app_table[app_id].active && app_table[app_id].queue != NULL;
}

bool swi_send_message(swi_app_id_t app_id, const swi_msg_t *msg, TickType_t wait_ticks)
{
	if (!is_valid_app(app_id)) return false;
	ESP_LOGD(TAG, "APP-ID: %d valid.", app_id);
	if (xQueueSend(app_table[app_id].queue, msg, wait_ticks) != pdTRUE) {
		ESP_LOGE(TAG, "Send Msg to APP-ID: %d failed.", app_id);
		return false;
	}
	// Wake app via TaskNotify (non-blocking)
	if (app_table[app_id].task) {
		xTaskNotify(app_table[app_id].task, 1, eSetBits);
	}
	return true;
}

bool swi_send_message_from_isr(swi_app_id_t app_id, const swi_msg_t *msg, BaseType_t *pxHigherPriorityTaskWoken)
{
	if (!is_valid_app(app_id)) return false;
	BaseType_t woke = pdFALSE;
	if (xQueueSendFromISR(app_table[app_id].queue, msg, &woke) != pdTRUE) {
		// queue full
		if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = woke;
		return false;
	}

	if (app_table[app_id].task) {
		xTaskNotifyFromISR(app_table[app_id].task, 1, eSetBits, &woke);
	}

	// propagate higher-prio flag
	if (pxHigherPriorityTaskWoken && woke) {
		*pxHigherPriorityTaskWoken = woke;
	}
	return true;
}

bool swi_recv_message(swi_app_id_t app_id, swi_msg_t *out_msg, TickType_t timeout_ticks)
{
	if (!is_valid_app(app_id) || !out_msg) return false;
	ESP_LOGD(TAG, "APP-ID: %d valid.", app_id);
	if (xQueueReceive(app_table[app_id].queue, out_msg, timeout_ticks) != pdTRUE) {
		ESP_LOGD(TAG, "No Messages in Queue");
		return false;
	}
	ESP_LOGD(TAG, "Messages in Queue");
	return true;
}

void swi_notify_app_from_isr(swi_app_id_t app_id, BaseType_t *pxHigherPriorityTaskWoken)
{
	if (!is_valid_app(app_id) || !app_table[app_id].task) {
		if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
		return;
	}
	BaseType_t woke = pdFALSE;
	xTaskNotifyFromISR(app_table[app_id].task, 1, eSetBits, &woke);
	if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = woke;
}