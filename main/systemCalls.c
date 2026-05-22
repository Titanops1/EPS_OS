#include "systemCalls.h"
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_elf.h"
#include "esp_log.h"
#include "private/elf_symbol.h"

#include "i2c_lib.h"
#include "memory.h"
#include "pin_def.h"
#include "swi.h"
#include "vga.h"

// Logging-Tag zur Identifikation von Log-Ausgaben
static const char *TAG = "APP LOADER";

// Standardpfad für ausführbare Apps im SPIFFS-Dateisystem
#define APP_PATH "/spiffs/application/"
#define APP_EXT  ".elf"

// Betriebsmodi für die System-LED
#define SYS_LED_HEARTBEAT  0  // Standard: Blinkt regelmäßig
#define SYS_LED_USER       1  // Benutzerdefiniert
uint8_t SysLedMode = SYS_LED_HEARTBEAT;
TaskHandle_t SysLed;  // Task-Handle für die LED-Steuerung

// Maximale Anzahl an Apps, die gleichzeitig geladen werden können
#define MAX_APPS 200

// Struktur zur Verwaltung laufender Apps
typedef struct {
	TaskHandle_t AppHandle;  // Task-Handle der App
	esp_elf_t elf;           // ELF-Datei-Daten
	int mem_size;            // Größe des reservierten Speichers
	uint8_t *exec_mem;       // Zeiger auf den ausführbaren Speicher
	char *name;              // Name der App
	uint8_t running;         // Status der App (0 = gestoppt, 1 = laufend)
	uint8_t id;			  	// ID der App
	uint8_t stderror;		// File-Descriptor für Standardfehlerausgabe
	swi_app_id_t app_id;
} App_t;

// Array zur Verwaltung aller Apps
App_t Apps[MAX_APPS];
int16_t AppStartCount = 0; // Zähler für gestartete Apps
uint16_t AppCount = 0;     // Gesamtanzahl geladener Apps

const struct esp_elfsym g_customer_elfsyms[] = {
	ESP_ELFSYM_EXPORT(snprintf),
	ESP_ELFSYM_EXPORT(printf),
	ESP_ELFSYM_EXPORT(fprintf),
	ESP_ELFSYM_EXPORT(delay_ms),
	ESP_ELFSYM_EXPORT(delay),
	ESP_ELFSYM_EXPORT(readGyroX),
	ESP_ELFSYM_EXPORT(readGyroY),
	ESP_ELFSYM_EXPORT(readGyroZ),
	ESP_ELFSYM_EXPORT(readAccelX),
	ESP_ELFSYM_EXPORT(readAccelY),
	ESP_ELFSYM_EXPORT(readAccelZ),
	ESP_ELFSYM_EXPORT(printNumber),
	ESP_ELFSYM_EXPORT(printString),
	ESP_ELFSYM_EXPORT(printChar),
	ESP_ELFSYM_EXPORT(printFloat),
	ESP_ELFSYM_EXPORT(printNewLine),

	ESP_ELFSYM_EXPORT(swi_get_notification),
	ESP_ELFSYM_EXPORT(swi_get_appId),
	ESP_ELFSYM_EXPORT(swi_send_message),
	ESP_ELFSYM_EXPORT(swi_send_message_from_isr),
	ESP_ELFSYM_EXPORT(swi_recv_message),
	ESP_ELFSYM_EXPORT(swi_notify_app_from_isr),

	//VGA Support
	ESP_ELFSYM_EXPORT(createForm),
	ESP_ELFSYM_EXPORT(destroyForm),
	ESP_ELFSYM_EXPORT(set_active_window),
	ESP_ELFSYM_EXPORT(get_active_window),
	ESP_ELFSYM_EXPORT(createTextbox),
	ESP_ELFSYM_EXPORT(createLabel),
	ESP_ELFSYM_EXPORT(window_add_widget),
	ESP_ELFSYM_EXPORT(textbox_set_text),
	ESP_ELFSYM_EXPORT(label_set_text),
	ESP_ELFSYM_EXPORT(createCircle),
	ESP_ELFSYM_EXPORT(circle_set_color),
	ESP_ELFSYM_EXPORT(circle_set_pos),
	ESP_ELFSYM_EXPORT(circle_set_radius),

	ESP_ELFSYM_EXPORT(vga_getWindowWidth),
	ESP_ELFSYM_EXPORT(vga_getWindowHeigth),
	ESP_ELFSYM_EXPORT(vga_get_frame_size),
	ESP_ELFSYM_EXPORT(vga_draw_pixel),
	ESP_ELFSYM_EXPORT(vga_draw_line),
	ESP_ELFSYM_EXPORT(vga_draw_rect),
	ESP_ELFSYM_EXPORT(vga_fill_rect),
	ESP_ELFSYM_EXPORT(vga_clear_screen),
	ESP_ELFSYM_EXPORT(vga_draw_circle),
	ESP_ELFSYM_EXPORT(vga_fill_circle),
	ESP_ELFSYM_EXPORT(vga_draw_text),
	ESP_ELFSYM_EXPORT(vga_draw_triangle),
	ESP_ELFSYM_EXPORT(vga_fill_triangle),
	ESP_ELFSYM_EXPORT(vga_swap_buffers),
	ESP_ELFSYM_END
};

float readGyroX()
{
	mpu6500_readGyroskop();
	return mpu6500_gyro[0];
}

float readGyroY()
{
	mpu6500_readGyroskop();
	return mpu6500_gyro[1];
}

float readGyroZ()
{
	mpu6500_readGyroskop();
	return mpu6500_gyro[2];
}

float readAccelX()
{
	mpu6500_readGyroskop();
	return mpu6500_accel[0];
}

float readAccelY()
{
	mpu6500_readGyroskop();
	return mpu6500_accel[1];
}

float readAccelZ()
{
	mpu6500_readGyroskop();
	return mpu6500_accel[2];
}

void printNumber(int number) {
	printf("%d", number);
}

void printString(const char *string) {
	printf("%s", string);
}

void printChar(char c) {
	printf("%c", c);
}

void printFloat(float f) {
	printf("%f", f);
}

void printNewLine() {
	printf("\n");
}

void delay_ms(int ms) {
	vTaskDelay(pdMS_TO_TICKS(ms));
}

void delay(int s) {
	vTaskDelay(pdMS_TO_TICKS(s * 1000));
}

uint16_t getAppsRunning() {
	return AppCount;
}

void close_app(uint8_t current_count, uint8_t terminate_code) {
	uint32_t notified;
	swi_msg_t msg;
	// Beende die App

	if(terminate_code > 0)
	{
		swi_msg_t m = {0};
		m.type = 0;
		m.data[0] = terminate_code;
		swi_send_message(Apps[current_count].app_id, &m, 0); // 0 ticks wait
	}
	vTaskDelay(pdMS_TO_TICKS(1000));

	if (swi_get_notification(&notified, 0) == 1) {
		// drain queue
		while (swi_recv_message(Apps[current_count].app_id, &msg, 0)) {
			// process message (ISR-safe content)
			ESP_LOGI(TAG, "App got signal type=%u, data[0]=%u\n", msg.type, msg.data[0]);
		}
	}

	if(terminate_code == 0)
	{
		swi_unregister_app(Apps[current_count].app_id);
		// Bereinigung und Freigabe von ELF-Ressourcen
		esp_elf_deinit(&Apps[current_count].elf);
		
		// Freigeben des zugewiesenen Speichers für den Code der App
		psram_free(Apps[current_count].exec_mem);
		
		// Markiere die App als 'nicht mehr laufend'
		Apps[current_count].running = 0;
		
		// Logge, dass die App beendet wurde
		ESP_LOGI(TAG, "App %s beendet", Apps[current_count].name);
		
		// Freigabe des Namens-Speichers der App
		psram_free(Apps[current_count].name);
		
		// Verringere den Zähler für laufende Apps
		AppCount--;
		
		Apps[current_count].mem_size = 0;
		Apps[current_count].exec_mem = NULL;
		Apps[current_count].app_id = -1;
		// Lösche den aktuellen Task (da die App beendet wurde)
		vTaskDelete(Apps[current_count].AppHandle);
		Apps[current_count].AppHandle = NULL;
	}
}

void start_app() {
	uint8_t current_count = AppStartCount;

	// Markiere die App als 'laufend'
	Apps[current_count].running = 1;
	
	TaskHandle_t me = xTaskGetCurrentTaskHandle();
	Apps[current_count].app_id = swi_register_app(me, 32);
	if (Apps[current_count].app_id < 0) {
		printf("app register failed\n");
		vTaskDelete(NULL);
	}

	// Initialisiere die ELF-Datei
	esp_elf_init(&Apps[current_count].elf);
		
	// Relokation der ELF-Datei (zugehörigen Code im Speicher anpassen)
	esp_elf_relocate(&Apps[current_count].elf, (const uint8_t *)Apps[current_count].exec_mem);
	
	// Anforderung der ELF-Datei (Initialisierung des App-Starts)
	esp_elf_request(&Apps[current_count].elf, 0, 0, NULL);
	close_app(current_count, 0);
}

int16_t checkAppRegister(const char *appname)
{
	for(uint16_t i = 0; i < MAX_APPS; i++)
	{
		//ESP_LOGI(TAG, "Check App %s with Filename %s", Apps[i].name, appname);
		if(strcmp(Apps[i].name, appname) == 0)
		{
			if(Apps[i].running == 1)
			{
				return -2;
			}
			else
			{
				return i;
			}
		}
	}
	return -1;
}

int16_t findFreeAppSlot()
{
	for(uint16_t i = 0; i < MAX_APPS; i++)
	{
		if(Apps[i].running == 0)
		{
			return i;
		}
	}
	return -1;
}

int registerApp(const char *appname)
{
	uint8_t skip_search = 0;
	char filename[128];

	//Check if App is already registered
	int16_t check = checkAppRegister(appname);
	if(check > -1)
	{
		ESP_LOGI(TAG, "App %s bereits registriert, wird neu geladen", appname);
		AppStartCount = check;
		skip_search = 1;
	}
	else if(check == -2)
	{
		ESP_LOGE(TAG, "App %s bereits registriert", appname);
		return -3;
	}

	//Search for free App Slot
	if(skip_search == 0)
	{
		AppStartCount = findFreeAppSlot();
		ESP_LOGI(TAG, "App Slot %d ist frei", AppStartCount);
	}

	if(AppStartCount == -1)
	{
		ESP_LOGE(TAG, "Kein freier App Slot gefunden");
		return -2;
	}

	//Load App
	Apps[AppStartCount].name = psram_malloc(strlen(appname) + 1);
	strcpy(Apps[AppStartCount].name, appname);
	sprintf(filename, "%s%s%s", APP_PATH, appname, APP_EXT);
	Apps[AppStartCount].exec_mem = spiffs_readApp(filename, &Apps[AppStartCount].mem_size);
	// FILE *file = fopen(filename, "rb");
	// if (!file) {
	// 	ESP_LOGE(TAG, "Datei %s konnte nicht geöffnet werden!", filename);
	// 	return -1;
	// }
	// Apps[AppStartCount].mem_size = fsize(file);
	// ESP_LOGI(TAG, "%d Bytes an Speicher werden Reserviert", Apps[AppStartCount].mem_size);
	// Apps[AppStartCount].exec_mem = psram_malloc(Apps[AppStartCount].mem_size);
	// fread(Apps[AppStartCount].exec_mem, 1, Apps[AppStartCount].mem_size, file);
	// fclose(file);
	xTaskCreate(start_app, Apps[AppStartCount].name, 4096, NULL, 5, &Apps[AppStartCount].AppHandle);
	ESP_LOGI(TAG, "App %s registriert", appname);
	AppCount++;
	return 1;
}

int unregisterApp(const char *appname) {
	for(uint16_t i = 0; i < MAX_APPS; i++)
	{
		if(strcmp(Apps[i].name, appname) == 0)
		{
			close_app(i, 9);
			ESP_LOGI(TAG, "App %s wurde entfernt", appname);
			return 1;
		}
	}
	ESP_LOGE(TAG, "App %s nicht gefunden", appname);
	return -1;
}

int registerSysApp(void *func, const char *appname)
{
	uint8_t skip_search = 0;

	//Check if App is already registered
	int16_t check = checkAppRegister(appname);
	if(check > -1)
	{
		ESP_LOGI(TAG, "App %s bereits registriert, wird neu geladen", appname);
		AppStartCount = check;
		skip_search = 1;
	}
	else if(check == -2)
	{
		ESP_LOGE(TAG, "App %s bereits registriert", appname);
		return -3;
	}

	//Search for free App Slot
	if(skip_search == 0)
	{
		AppStartCount = findFreeAppSlot();
		ESP_LOGI(TAG, "App Slot %d ist frei", AppStartCount);
	}

	if(AppStartCount == -1)
	{
		ESP_LOGE(TAG, "Kein freier App Slot gefunden");
		return -2;
	}

	//Load App
	//Apps[AppStartCount].name = appname;
	Apps[AppStartCount].name = psram_malloc(strlen(appname) + 1);
	strcpy(Apps[AppStartCount].name, appname);
	Apps[AppStartCount].exec_mem = 0;
	Apps[AppStartCount].mem_size = 0;
	Apps[AppStartCount].running = 1;
	xTaskCreate(func, Apps[AppStartCount].name, 4096, NULL, 5, &Apps[AppStartCount].AppHandle);
	Apps[AppStartCount].app_id = swi_register_app(Apps[AppStartCount].AppHandle, 32);
	if (Apps[AppStartCount].app_id < 0) {
		printf("Sysapp register failed\n");
		vTaskDelete(Apps[AppStartCount].AppHandle);
	}
	ESP_LOGI(TAG, "System App %s registriert", appname);
	return 1;
}

int unregisterSysApp(const char *appname) {
	for(uint16_t i = 0; i < MAX_APPS; i++)
	{
		if(strcmp(Apps[i].name, appname) == 0)
		{
			// Freigabe des Namens-Speichers der App
			psram_free(Apps[i].name);
			swi_unregister_app(Apps[i].app_id);
			// Lösche den aktuellen Task (da die App beendet wurde)
			vTaskDelete(Apps[i].AppHandle);
			Apps[i].AppHandle = NULL;
			Apps[i].mem_size = 0;
			Apps[i].exec_mem = NULL;
			Apps[i].app_id = -1;

			ESP_LOGI(TAG, "App %s wurde entfernt", appname);
			return 1;
		}
	}
	ESP_LOGE(TAG, "App %s nicht gefunden", appname);
	return -1;
}

int printAppList(int argc, char **argv) {
	printf("App List:\n");
	for(uint16_t i = 0; i < MAX_APPS; i++)
	{
		if(Apps[i].running == 1)
		{
			printf("App %d: %s\n", i, Apps[i].name);
		}
	}
	return 0;
}

void initApps()
{
	swi_init();
	ESP_LOGI(TAG, "Init App Locators");
	for(uint16_t i = 0; i < MAX_APPS; i++)
	{
		Apps[i].AppHandle = NULL;
		esp_elf_deinit(&Apps[i].elf);
		Apps[i].mem_size = 0;
		Apps[i].exec_mem = NULL;
		Apps[i].name = "";
		Apps[i].running = 0;
		Apps[i].id = 0xFF;
		Apps[i].stderror = 0xFF;
		Apps[i].app_id = -1;
	}
}