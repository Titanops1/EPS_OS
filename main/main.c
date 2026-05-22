#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include <driver/gpio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_netif.h"

#include "nvs_flash.h"
#include "esp_spiffs.h"

#include "shell.h"
#include "wifi.h"
#include "uart_lib.h"
#include "i2c_lib.h"
#include "http_ota.h"
#include "systemCalls.h"
#include "os_commands.h"
#include "ota_update.h"
#include "vga.h"
#include "window_manager.h"
#include "memory.h"

#include "explorer.h"

#include "pin_def.h"
#include "../../register_def.h"

/*******************
*I2C SDA=18 SCL=21
*SPI SCK=14 MISO=35 MOSI=12 CS=15
*I2S CLK=27 DO=25
*RP2040 SWD SWCLK=4 SWDIO=2 ENABLE=5 ENABLE PIN High=SWD over ESP
*RP2040 UART RPI_TX=22 RPI_RX=19
*RP2040 RST=23
*INT=34
*UART2 TX=13 RX=26
********************/

static const char *TAG = "main";

TaskHandle_t init_spiffs_handle;

/****************************************
 * CORE 0 Beginn
 * Main Application
*/

void boot_logo_animation_dynamic() {
	#define FPS_BOOT	20
	#define BOOT_TIME	5000 //in ms
	uint16_t time_cnt = 0;
	uint8_t frame = 0;
	int x = vga_getWindowWidth()/2;
	int y = vga_getWindowHeigth()/2;

	while(time_cnt < BOOT_TIME/(1000/FPS_BOOT)) {
		vga_clear_screen(0,0,0);  // schwarzer Hintergrund

		// Pulsierender Kreis als Logo
		int radius = 20 + (frame % 20);  // Radius zwischen 20..39
		vga_fill_circle(x, y, radius, 255, 0, 0, 255);

		// Drehendes Dreieck
		float angle = (frame * 0.1f);
		int x1 = x + 30*cosf(angle);
		int y1 = y + 30*sinf(angle);
		int x2 = x + 30*cosf(angle + 2.09f);
		int y2 = y + 30*sinf(angle + 2.09f);
		int x3 = x + 30*cosf(angle + 4.18f);
		int y3 = y + 30*sinf(angle + 4.18f);
		vga_fill_triangle(x1, y1, x2, y2, x3, y3, 0, 255, 0, 255);

		// Pulsierendes Rechteck
		int w = 50 + (frame % 15);
		int h = 30 + (frame % 15);
		vga_fill_rect(x-w/2, y+h/2, w, h, 0, 0, 255, 255);

		// Buffer swap
		vga_swap_buffers();

		frame++;
		vTaskDelay(pdMS_TO_TICKS(1000/FPS_BOOT));  // 20 FPS
		time_cnt++;
	}
}

void init_spiffs(void *arg)
{
	gpio_set_direction(BLUE_LED_PIN, GPIO_MODE_OUTPUT);
	xTaskCreatePinnedToCore(sys_led_task, "sys_led_task", 1024, NULL, 24, NULL, 0);
	ESP_LOGI(TAG, "Initializing SPIFFS");

	esp_vfs_spiffs_conf_t conf = {
	  .base_path = "/spiffs",
	  .partition_label = NULL,
	  .max_files = 20,
	  .format_if_mount_failed = true
	};

	// Use settings defined above to initialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
	sys_access();
	esp_err_t ret = esp_vfs_spiffs_register(&conf);
	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
		}
		init_spiffs_handle = NULL;
		vTaskDelete(NULL);
		return;
	}

	ESP_LOGI(TAG, "Performing SPIFFS_check().");
	sys_access();
	ret = esp_spiffs_check(conf.partition_label);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
		init_spiffs_handle = NULL;
		vTaskDelete(NULL);
		return;
	} else {
		ESP_LOGI(TAG, "SPIFFS_check() successful");
	}

	size_t total = 0, used = 0;
	sys_access();
	ret = esp_spiffs_info(conf.partition_label, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
		sys_access();
		esp_spiffs_format(conf.partition_label);
		init_spiffs_handle = NULL;
		vTaskDelete(NULL);
		return;
	} else {
		ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
		char spiffs_info[64];
		snprintf(spiffs_info, sizeof(spiffs_info), "Total: %d bytes | Used: %d bytes", total, used);
		drawText(spiffs_info, 255, 255, 255, 255);
	}

	// Check consistency of reported partiton size info.
	if (used > total) {
		ESP_LOGW(TAG, "Number of used bytes cannot be larger than total. Performing SPIFFS_check().");
		sys_access();
		ret = esp_spiffs_check(conf.partition_label);
		// Could be also used to mend broken files, to clean unreferenced pages, etc.
		// More info at https://github.com/pellepl/spiffs/wiki/FAQ#powerlosses-contd-when-should-i-run-spiffs_check
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
			init_spiffs_handle = NULL;
			vTaskDelete(NULL);
			return;
		} else {
			ESP_LOGI(TAG, "SPIFFS_check() successful");
		}
	}
	init_spiffs_handle = NULL;
	vTaskDelete(NULL);
}

void init_console(void)
{
	esp_log_level_set("*", ESP_LOG_NONE);
	register_commands();
	shell_init();
}

void app_main(void) {
	int main_id = 0;

	gpio_set_direction (RP2040_RST_PIN, GPIO_MODE_OUTPUT);
	gpio_set_level(RP2040_RST_PIN, 0);


	ESP_LOGI(TAG, "[APP] Startup..");
	ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
	ESP_LOGI("PSRAM", "Gesamter PSRAM: %d Bytes", psram_getSize());
	ESP_LOGI("PSRAM", "Freier PSRAM: %d Bytes", psram_getFree());

	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	window_manager_init();
	ota_show_status();

	main_id = window_register("Main", xTaskGetCurrentTaskHandle(), NULL, NULL);
	focus_request(main_id);

	gpio_set_level(RP2040_RST_PIN, 1);
	init_vga();
	
	printf("Window Size: %dx%d\n", vga_getWindowWidth(), vga_getWindowHeigth());

	vga_clear_screen(0, 0, 0);  // schwarzer Hintergrund
	// Titel
	setCursor(10, 2);
	drawText("ESPOS Operating System", 255, 255, 255, 255);  // weiß

	setCursorNextLine();
	vga_draw_line(0, getYCursor(), 640, getYCursor(), 0, 255, 0, 255);
	vga_swap_buffers();
	// Infos
	setCursorNextLine();
	drawText(">> Initializing subsystems...", 0, 255, 255, 255);

	setCursorNextLine();
	setCursorNextLine();
	drawText(">> PSRAM: ", 255, 255, 0, 255);
	vga_swap_buffers();

	char psram_info[64];
	snprintf(psram_info, sizeof(psram_info), "Total: %d bytes | Free: %d bytes",
			psram_getSize(),
			psram_getFree());
	drawText(psram_info, 255, 255, 255, 255);

	setCursorNextLine();
	drawText(">> SPIFFS: ", 255, 255, 0, 255);
	vga_swap_buffers();

	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	  nvs_flash_erase();
	  ret = nvs_flash_init();
	}
	xTaskCreate(init_spiffs, "Spiffs Init", 4096, NULL, 1, &init_spiffs_handle);
	float angle = 0.0f;
	while(init_spiffs_handle != NULL)
	{
		vga_fill_circle(vga_getWindowWidth()/2, vga_getWindowHeigth()/2, 45, 0, 0, 0, 255);
		draw_spinner(vga_getWindowWidth()/2, vga_getWindowHeigth()/2, 40, angle);
		vga_swap_buffers();

		angle += 30.0f;       // Rotationsgeschwindigkeit
		if (angle >= 360.0f)  // einmal rum
			angle = 0.0f;

		vTaskDelay(pdMS_TO_TICKS(80));  // 80 ms Pause → flüssig
	}
	vga_fill_circle(vga_getWindowWidth()/2, vga_getWindowHeigth()/2, 45, 0, 0, 0, 255);

	setCursorNextLine();
	drawText(">> Init Apps...", 0, 255, 255, 255);
	vga_swap_buffers();
	initApps();
	
	setCursorNextLine();
	drawText(">> Wi-Fi: Connecting...", 255, 255, 0, 255);
	setCursorNextLine();
	vga_swap_buffers();
	wifi_init_sta();

	check_and_update_firmware(0);

	i2c_init();
	ESP_LOGI(TAG, "[APP] Tasks activated %d", uxTaskGetNumberOfTasks());
	
	setCursorNextLine();
	drawText("Bootloader v1.0 - Build 2025.11", 0, 0, 255, 255);
	setCursorNextLine();
	drawText("RP2040 GPU online", 0, 255, 0, 255);
	setCursorNextLine();
	drawText("ESP32 Host initialized", 0, 255, 0, 255);
	setCursorNextLine();
	vga_draw_line(0, getYCursor(), 640, getYCursor(), 0, 255, 0, 255);
	vga_swap_buffers();
	setCursorNextLine();

	focus_release(main_id);

	init_console();
	init_explorer(vga_getWindowWidth(), vga_getWindowHeigth());
	while (1) {
		// if(getRxComplete())
		// {
		// 	if(getRxReg() == REG_TOUCH)
		// 	{
		// 		printf("Touch X: %d Touch Y: %d Pressed: %d\n", getRxData(0), getRxData(1), getRxData(2));
		// 		clearRxComplete();
		// 	}
		// }
		vTaskDelay(pdMS_TO_TICKS(500));
	}
	window_unregister(main_id);
}