#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include <driver/gpio.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp32/himem.h"

#include "nvs_flash.h"
#include "esp_spiffs.h"

#include "memory.h"
#include "pin_def.h"

uint8_t disk_activity = 0;
SemaphoreHandle_t fs_mutex;
TaskHandle_t init_spiffs_handle;
spiffs_fs_t fs_memory;

void printMemorySize(uint32_t size)
{
	if(size < 1024)
	{
		printf("%ld Byte", size);
	}
	else if(size < 1024*1024)
	{
		printf("%.2f kByte", (float)(size)/1024.0f);
	}
	else
	{
		printf("%.2f MByte", (float)(size)/(1024.0f*1024.0f));
	}
}

void print_heap_info() {
	printf("Heap Gesamt:      ");
	printMemorySize(heap_caps_get_total_size(MALLOC_CAP_8BIT) + esp_himem_get_phys_size());
	printf("\n");
	printf("Freier Heap:      ");
	printMemorySize(heap_caps_get_free_size(MALLOC_CAP_8BIT) + esp_himem_get_free_size());
	printf("\n");
	printf("Größter Block:    ");
	printMemorySize(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
	printf("\n");
	printf("Interner Heap:    ");
	printMemorySize(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
	printf("\n");
	printf("Externer Heap:    ");
	printMemorySize(psram_getFree());
	printf("\n");
}

int psram_getSize()
{
	return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) + esp_himem_get_phys_size();
}

int psram_getFree()
{
	return heap_caps_get_free_size(MALLOC_CAP_SPIRAM) + esp_himem_get_free_size();
}

void *psram_malloc(size_t size)
{
	return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

void psram_free(void *ptr)
{
	heap_caps_free(ptr);
}

void *psram_realloc(void *ptr, size_t size)
{
	return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
}

himem_block_t* mm_himem_alloc(size_t size)
{
	himem_block_t* blk = malloc(sizeof(himem_block_t));
	if (!blk) return NULL;

	if (esp_himem_alloc(size, &blk->handle) != ESP_OK) {
		free(blk);
		return NULL;
	}

	// create map range (one-time)
	if (esp_himem_alloc_map_range(ESP_HIMEM_BLKSZ, &blk->range) != ESP_OK) {
		esp_himem_free(blk->handle);
		free(blk);
		return NULL;
	}

	blk->size = size;
	blk->offset = 0;
	return blk;
}

void mm_himem_read(himem_block_t* blk, size_t offset, void* dst, size_t len)
{
	uint8_t *ptr;
	size_t pos = 0;

	//esp_himem_alloc_map_range(ESP_HIMEM_BLKSZ, &blk->range);

	while (pos < len) {
		size_t blk_off = (offset + pos) & (ESP_HIMEM_BLKSZ - 1);
		size_t blk_base = (offset + pos) - blk_off;
		size_t chunk = ESP_HIMEM_BLKSZ - blk_off;
		if (chunk > len - pos) chunk = len - pos;

		ESP_ERROR_CHECK(esp_himem_map(
			blk->handle,
			blk->range,
			blk_base,
			0,
			ESP_HIMEM_BLKSZ,
			0,
			(void**)&ptr
		));

		memcpy((uint8_t*)dst + pos, ptr + blk_off, chunk);

		ESP_ERROR_CHECK(esp_himem_unmap(
			blk->range,
			ptr,
			ESP_HIMEM_BLKSZ
		));

		pos += chunk;
	}

	//esp_himem_free_map_range(blk->range);
}

void mm_himem_write(himem_block_t* blk, size_t offset, const void* src, size_t len)
{
	uint8_t *ptr;
	size_t pos = 0;

	// Map range for 1 block
	//esp_himem_alloc_map_range(ESP_HIMEM_BLKSZ, &blk->range);

	while (pos < len) {
		size_t blk_off = (offset + pos) & (ESP_HIMEM_BLKSZ - 1); // offset im Block
		size_t blk_base = (offset + pos) - blk_off;              // Blockanfang
		size_t chunk = ESP_HIMEM_BLKSZ - blk_off;                // freier Platz im Block
		if (chunk > len - pos) chunk = len - pos;

		// --- 32KB BLOCK MAPPEN ---
		ESP_ERROR_CHECK(esp_himem_map(
			blk->handle,
			blk->range,
			blk_base,
			0,
			ESP_HIMEM_BLKSZ,
			0,
			(void**)&ptr
		));

		// --- Schreibvorgang in den 32KB-Speicher ---
		memcpy(ptr + blk_off, (uint8_t*)src + pos, chunk);

		// --- Block unmap ---
		ESP_ERROR_CHECK(esp_himem_unmap(
			blk->range,
			ptr,
			ESP_HIMEM_BLKSZ
		));

		pos += chunk;
	}

	//esp_himem_free_map_range(blk->range);
	blk->offset += len;
}

void mm_himem_free(himem_block_t* blk)
{
	esp_himem_free_map_range(blk->range);
	esp_himem_free(blk->handle);
	free(blk);
}

//Filesystem
void sys_led(int value) {
	gpio_set_level(BLUE_LED_PIN, value);
}

void begin_spiffs_access()
{
	disk_activity++;
}

void end_spiffs_access()
{
	disk_activity--;
}

//To Do OS Filesystem
void init_spiffs(void *arg)
{
	gpio_set_direction(BLUE_LED_PIN, GPIO_MODE_OUTPUT);
	xTaskCreatePinnedToCore(sys_led_task, "sys_led_task", 1024, NULL, 24, NULL, 0);
	fs_mutex = xSemaphoreCreateMutex();
	ESP_LOGI("SPIFFS APP", "Initializing SPIFFS");

	esp_vfs_spiffs_conf_t conf = {
	  .base_path = "/spiffs",
	  .partition_label = NULL,
	  .max_files = 20,
	  .format_if_mount_failed = true
	};

	// Use settings defined above to initialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
	SPIFFS_BEGIN();
	esp_err_t ret = esp_vfs_spiffs_register(&conf);
	SPIFFS_END();
	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE("SPIFFS APP", "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE("SPIFFS APP", "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE("SPIFFS APP", "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
		}
		init_spiffs_handle = NULL;
		vTaskDelete(NULL);
		return;
	}

	ESP_LOGI("SPIFFS APP", "Performing SPIFFS_check().");
	SPIFFS_BEGIN();
	ret = esp_spiffs_check(conf.partition_label);
	SPIFFS_END();
	if (ret != ESP_OK) {
		ESP_LOGE("SPIFFS APP", "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
		init_spiffs_handle = NULL;
		vTaskDelete(NULL);
		return;
	} else {
		ESP_LOGI("SPIFFS APP", "SPIFFS_check() successful");
	}

	size_t total = 0, used = 0;
	SPIFFS_BEGIN();
	ret = esp_spiffs_info(conf.partition_label, &total, &used);
	SPIFFS_END();
	if (ret != ESP_OK) {
		ESP_LOGE("SPIFFS APP", "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
		SPIFFS_BEGIN();
		esp_spiffs_format(conf.partition_label);
		SPIFFS_END();
		init_spiffs_handle = NULL;
		vTaskDelete(NULL);
		return;
	} else {
		ESP_LOGI("SPIFFS APP", "Partition size: total: %d, used: %d", total, used);
		fs_memory.fs_total = total;
		fs_memory.fs_used = used;
		fs_memory.fs_free = total-used;
	}

	// Check consistency of reported partiton size info.
	if (used > total) {
		ESP_LOGW("SPIFFS APP", "Number of used bytes cannot be larger than total. Performing SPIFFS_check().");
		SPIFFS_BEGIN();
		ret = esp_spiffs_check(conf.partition_label);
		SPIFFS_END();
		// Could be also used to mend broken files, to clean unreferenced pages, etc.
		// More info at https://github.com/pellepl/spiffs/wiki/FAQ#powerlosses-contd-when-should-i-run-spiffs_check
		if (ret != ESP_OK) {
			ESP_LOGE("SPIFFS APP", "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
			init_spiffs_handle = NULL;
			vTaskDelete(NULL);
			return;
		} else {
			ESP_LOGI("SPIFFS APP", "SPIFFS_check() successful");
		}
	}
	init_spiffs_handle = NULL;
	vTaskDelete(NULL);
}

// int fs_read_file(const char *path, void **buffer, size_t *size);

// int fs_write_file(const char *path, const void *data, size_t size);

void fs_move(const char *src, const char *dst)
{
	SPIFFS_BEGIN();
	remove(dst);
	rename(src, dst);
	remove(src);
	SPIFFS_END();
}

void fs_remove(const char *src)
{
	SPIFFS_BEGIN();
	remove(src);
	SPIFFS_END();
}

FILE *fs_open(const char *path, const char *mode)
{
	xSemaphoreTake(fs_mutex, portMAX_DELAY);
	SPIFFS_BEGIN();
	FILE *file = fopen(path, mode);
	if (!file) {
		ESP_LOGE("SPIFFS APP", "Datei %s konnte nicht geöffnet werden!", path);
		SPIFFS_END();
		xSemaphoreGive(fs_mutex);
		return NULL;
	}
	xSemaphoreGive(fs_mutex);
	return file;
}

int fs_read(FILE *f, void *buf, size_t size)
{
	xSemaphoreTake(fs_mutex, portMAX_DELAY);
	size_t read_bytes = fread(buf, 1, size, f);
	xSemaphoreGive(fs_mutex);
	return read_bytes;
}

int fs_write(FILE *f, const void *buf, size_t size)
{
	xSemaphoreTake(fs_mutex, portMAX_DELAY);
	size_t bytes_written = fwrite(buf, 1, size, f);
	xSemaphoreGive(fs_mutex);
	return bytes_written;
}

char *fs_gets(FILE *f, char *buf, size_t size)
{
	xSemaphoreTake(fs_mutex, portMAX_DELAY);
	char *ret = NULL;
	if(f)
		ret = fgets(buf, size, f);
	xSemaphoreGive(fs_mutex);
	return ret;

}

void fs_close(FILE *f)
{
	xSemaphoreTake(fs_mutex, portMAX_DELAY);
	fclose(f);
	SPIFFS_END();
	xSemaphoreGive(fs_mutex);
}

int fsize(FILE *file) {
	sys_access();
	fseek(file, 0, SEEK_END);
	int size = ftell(file);
	sys_access();
	fseek(file, 0, SEEK_SET);
	return size;
}

void *spiffs_readApp(const char *filename, int *out_size)
{
	sys_access();
	FILE *file = fopen(filename, "rb");
	if (!file) {
		ESP_LOGE("SPIFFS APP", "Datei %s konnte nicht geöffnet werden!", filename);
		return NULL;
	}

	// Dateigröße bestimmen
	sys_access();
	fseek(file, 0, SEEK_END);
	size_t file_size = ftell(file);
	sys_access();
	fseek(file, 0, SEEK_SET);

	if (out_size)
		*out_size = file_size;

	ESP_LOGI("SPIFFS APP", "Dateigroesse: %u Bytes -> reserviere PSRAM...", (unsigned)file_size);

	// PSRAM-Speicher reservieren
	void *mem = psram_malloc(file_size);
	if (!mem) {
		ESP_LOGE("SPIFFS APP", "PSRAM malloc fehlgeschlagen (%u Bytes)", (unsigned)file_size);
		fclose(file);
		return NULL;
	}

	// Datei vollständig einlesen
	sys_access();
	size_t read_bytes = fread(mem, 1, file_size, file);
	sys_access();
	fclose(file);

	if (read_bytes != file_size) {
		ESP_LOGE("SPIFFS APP", "Fehler beim Lesen: %u / %u Bytes", (unsigned) read_bytes, (unsigned) file_size);
		free(mem);
		return NULL;
	}

	ESP_LOGI("SPIFFS APP", "Datei geladen und in PSRAM gespeichert.");

	return mem;
}

void sys_access()
{
	disk_activity = 0;
}

void sys_led_task(void *arg)
{
	while (1) {
		if(disk_activity > 0) {
			gpio_set_level(BLUE_LED_PIN, 1);
		}else {
			gpio_set_level(BLUE_LED_PIN, 0);
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}