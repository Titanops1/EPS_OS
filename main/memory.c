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

uint8_t sys_led_count = 10;

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
	sys_led_count = 0;
}

void sys_led_task(void *arg)
{
	while (1) {
		if(sys_led_count < 10) {
			gpio_set_level(BLUE_LED_PIN, 1);
			sys_led_count++;
		}else {
			gpio_set_level(BLUE_LED_PIN, 0);
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}