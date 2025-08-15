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

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_netif.h"

#include "nvs_flash.h"
#include "esp_spiffs.h"

#include "wifi.h"
#include "uart_lib.h"
#include "i2c_lib.h"
#include "http_ota.h"
#include "systemCalls.h"
#include "os_commands.h"
#include "ota_update.h"
#include "vga.h"

#include "pin_def.h"

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

/****************************************
 * CORE 0 Beginn
 * Main Application
*/
void print_bootscreen()
{
	vga_clear_screen(0, 0, 0);  // schwarzer Hintergrund

	// Titel
	vga_draw_text(10, 2, "ESPOS Operating System", 1, 1, 1);  // weiß

	vga_draw_line(0, 10, 640, 10, 0, 1, 0);
	vga_draw_line(0, 82, 640, 82, 0, 1, 0);

	// Infos
	vga_draw_text(2, 18, ">> Initializing subsystems...", 0, 1, 1);

	vga_draw_text(2, 26, ">> PSRAM:", 1, 1, 0);

	char psram_info[64];
	snprintf(psram_info, sizeof(psram_info), "Total: %d bytes | Free: %d bytes",
			heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
	vga_draw_text(2, 34, psram_info, 1, 1, 1);

	vga_draw_text(2, 42, ">> SPIFFS:", 1, 1, 0);
	// Du kannst hier aktuelle Infos aus SPIFFS anzeigen, z. B. Partition size

	vga_draw_text(2, 50, ">> Wi-Fi: Connecting...", 1, 1, 0);

	vga_draw_text(2, 58, "Bootloader v1.0 - Build 2025.08", 0, 0, 1);
	vga_draw_text(2, 66, "RP2040 GPU online", 0, 1, 0);
	vga_draw_text(2, 74, "ESP32 Host initialized", 0, 1, 0);

	vga_draw_text(10, 90, "Press any key to enter setup", 1, 1, 1);
}

// Einfacher Himmel + Boden
void draw_landscape() {
	// Himmel: Blau
	vga_fill_rect(0, 0, 320, 60, 0, 0, 1);

	// Boden: Grün
	vga_fill_rect(0, 60, 320, 60, 0, 1, 0);

	// Sonne: Gelber Kreis
	vga_fill_circle(280, 20, 10, 1, 1, 0);

	// Baumstamm (braun)
	for (int y = 80; y < 100; y++) {
		for (int x = 50; x < 54; x++) {
			vga_draw_pixel(x, y, 139, 69, 19);
		}
	}

	// Baumkrone (grün)
	for (int y = -8; y <= 0; y++) {
		for (int x = -8; x <= 8; x++) {
			if (x*x + y*y <= 64) {
				vga_draw_pixel(52 + x, 80 + y, 0, 200, 0);
			}
		}
	}
}

void run_graphics_test() {
	vga_clear_screen(0, 0, 0);

	// Farbverlauf von oben nach unten (rot)
	for (int y = 0; y < 480; y += 10) {
		vga_draw_rect(0, y, 640, 10, y / 2, 0, 0);  // Rotverlauf
	}

	// Diagonale Linien von links oben nach rechts unten
	for (int i = 0; i < 640; i += 40) {
		vga_draw_line(0, 0, i, 479, 0, 255, 0);  // Grün
	}

	for (int i = 0; i < 480; i += 40) {
		vga_draw_line(0, 0, 639, i, 0, 255, 0);  // Weitere grüne Linien
	}

	// Rechtecke in verschiedenen Farben
	vga_draw_rect(100, 100, 100, 50, 255, 0, 0);   // Rot
	vga_draw_rect(250, 100, 100, 50, 0, 255, 0);   // Grün
	vga_draw_rect(400, 100, 100, 50, 0, 0, 255);   // Blau

	// Textanzeige
	vga_draw_text(20, 300, "VGA Testbild", 255, 255, 255);
	vga_draw_text(20, 320, "RP2040 GPU Test", 255, 255, 0);
}

void init_spiffs(void)
{
	ESP_LOGI(TAG, "Initializing SPIFFS");

	esp_vfs_spiffs_conf_t conf = {
	  .base_path = "/spiffs",
	  .partition_label = NULL,
	  .max_files = 20,
	  .format_if_mount_failed = true
	};

	// Use settings defined above to initialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
	esp_err_t ret = esp_vfs_spiffs_register(&conf);
	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
		}
		return;
	}

	ESP_LOGI(TAG, "Performing SPIFFS_check().");
	ret = esp_spiffs_check(conf.partition_label);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
		return;
	} else {
		ESP_LOGI(TAG, "SPIFFS_check() successful");
	}

	size_t total = 0, used = 0;
	ret = esp_spiffs_info(conf.partition_label, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
		esp_spiffs_format(conf.partition_label);
		return;
	} else {
		ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
	}

	// Check consistency of reported partiton size info.
	if (used > total) {
		ESP_LOGW(TAG, "Number of used bytes cannot be larger than total. Performing SPIFFS_check().");
		ret = esp_spiffs_check(conf.partition_label);
		// Could be also used to mend broken files, to clean unreferenced pages, etc.
		// More info at https://github.com/pellepl/spiffs/wiki/FAQ#powerlosses-contd-when-should-i-run-spiffs_check
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
			return;
		} else {
			ESP_LOGI(TAG, "SPIFFS_check() successful");
		}
	}
}

void init_console(void)
{
	//#define PROMPT_STR CONFIG_IDF_TARGET

	esp_console_repl_t *repl = NULL;
	esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
	/* Prompt to be printed before each line.
	 * This can be customized, made dynamic, etc.
	 */
	repl_config.task_stack_size = 8192;
	repl_config.prompt = "esp os>";//PROMPT_STR ">";
	repl_config.max_cmdline_length = 1024;//CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH;

	/* Register commands */
	esp_console_register_help_command();
	register_commands();
	//register_system_common();

	esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

	ESP_ERROR_CHECK(esp_console_start_repl(repl));
	esp_log_level_set("*", ESP_LOG_NONE);
}

void app_main(void) {
	gpio_set_direction (RP2040_RST_PIN, GPIO_MODE_OUTPUT);
	gpio_set_level(RP2040_RST_PIN, 0);


	ESP_LOGI(TAG, "[APP] Startup..");
	ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
	ESP_LOGI("PSRAM", "Gesamter PSRAM: %d Bytes", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
	ESP_LOGI("PSRAM", "Freier PSRAM: %d Bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	ota_show_status();

	gpio_set_level(RP2040_RST_PIN, 1);
	rpi_uart_init(0, configMAX_PRIORITIES-1);

	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	  nvs_flash_erase();
	  ret = nvs_flash_init();
	}
	init_spiffs();

	init_systemcalls();
	initApps();
	
	wifi_init_sta();

	check_and_update_firmware(0);

	print_bootscreen();
	i2c_init();
	ESP_LOGI(TAG, "[APP] Tasks activated %d", uxTaskGetNumberOfTasks());
	
	vTaskDelay(pdMS_TO_TICKS(5000));
	//run_graphics_test();
	//draw_landscape();
	init_console();
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(2000));
	}
}