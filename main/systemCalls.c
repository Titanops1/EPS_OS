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
#include "pin_def.h"

// Logging-Tag zur Identifikation von Log-Ausgaben
static const char *TAG = "APP LOADER";

// Standardpfad für ausführbare Apps im SPIFFS-Dateisystem
#define APP_PATH "/spiffs/"
#define APP_EXT  ".elf"

// System-LED-Steuerung mit Mutex
SemaphoreHandle_t SysLedMutex = NULL;

// Betriebsmodi für die System-LED
#define SYS_LED_HEARTBEAT  0  // Standard: Blinkt regelmäßig
#define SYS_LED_USER       1  // Benutzerdefiniert
uint8_t SysLedMode = SYS_LED_HEARTBEAT;
TaskHandle_t SysLed;  // Task-Handle für die LED-Steuerung

// Maximale Anzahl an Apps, die gleichzeitig geladen werden können
#define MAX_APPS 10

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
} App_t;

// Array zur Verwaltung aller Apps
App_t Apps[MAX_APPS];
int16_t AppStartCount = 0; // Zähler für gestartete Apps
uint16_t AppCount = 0;     // Gesamtanzahl geladener Apps

// --- Interprozesskommunikation (IPC) ---

// Maximale Anzahl von IPC-Queues
#define MAX_QUEUES 255
// Maximale Nachrichtenlänge in der IPC-Queue
#define IPC_MSG_MAX_LEN 64
// Maximale Länge eines Queue-Namens
#define MAX_QUEUE_NAME_LEN 16

// Struktur zur Verwaltung einer IPC-Queue
typedef struct {
	int fd;                         // File-Descriptor der Queue
	char name[MAX_QUEUE_NAME_LEN];  // Name der Queue
	QueueHandle_t queue;            // FreeRTOS-Queue-Handle
} IPCQueueEntry;

// Array zur Verwaltung der IPC-Queues
static IPCQueueEntry ipc_queues[MAX_QUEUES];

const struct esp_elfsym elf_symbols[] = {
	ESP_ELFSYM_EXPORT(snprintf),
	ESP_ELFSYM_EXPORT(printf),
	ESP_ELFSYM_EXPORT(fprintf),
	ESP_ELFSYM_EXPORT(sys_led),
	ESP_ELFSYM_EXPORT(sys_led_mode),
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
	ESP_ELFSYM_EXPORT(sys_openqueue),
	ESP_ELFSYM_EXPORT(sys_findqueue),
	ESP_ELFSYM_EXPORT(sys_closequeue),
	ESP_ELFSYM_EXPORT(sys_sendmsg),
	ESP_ELFSYM_EXPORT(sys_recvmsg),
	ESP_ELFSYM_END
};

// System-Call: Neue Queue mit Namen öffnen
int sys_openqueue(const char *name) {
	for (int i = 0; i < MAX_QUEUES; i++) {
		if (ipc_queues[i].queue == NULL) {
			ipc_queues[i].queue = xQueueCreate(10, IPC_MSG_MAX_LEN);
			ipc_queues[i].fd = i;
			strncpy(ipc_queues[i].name, name, MAX_QUEUE_NAME_LEN - 1);
			ipc_queues[i].name[MAX_QUEUE_NAME_LEN - 1] = '\0';
			return ipc_queues[i].fd;
		}
	}
	return -1;  // Kein Platz mehr für neue Queues
}

// System-Call: Bestehende Queue mit Namen finden
int sys_findqueue(const char *name) {
	for (int i = 0; i < MAX_QUEUES; i++) {
		if (ipc_queues[i].queue != NULL && strncmp(ipc_queues[i].name, name, MAX_QUEUE_NAME_LEN) == 0) {
			//printf("Queue %d %d %s gefunden\n", i, ipc_queues[i].fd, ipc_queues[i].name);
			return ipc_queues[i].fd;
		}
	}
	return -1;  // Keine Queue mit diesem Namen gefunden
}

// System-Call: Queue schließen
int sys_closequeue(int fd) {
	for (int i = 0; i < MAX_QUEUES; i++) {
		if (ipc_queues[i].fd == fd) {
			vQueueDelete(ipc_queues[i].queue);
			ipc_queues[i].queue = NULL;
			ipc_queues[i].fd--;
			ipc_queues[i].name[0] = '\0';
			return 0;
		}
	}
	return -1;  // fd nicht gefunden
}

// System-Call: Nachricht senden
int sys_sendmsg(int fd, const char *msg, size_t len) {
	for (int i = 0; i < MAX_QUEUES; i++) {
		if (ipc_queues[i].fd == fd) {
			char buffer[IPC_MSG_MAX_LEN];
			strncpy(buffer, msg, len);
			buffer[len] = '\0';
			//printf("Nachricht senden über Queue %d: %s\n", i, buffer);
			return xQueueSend(ipc_queues[i].queue, buffer, pdMS_TO_TICKS(2000)) == pdTRUE ? 0 : -1;
		}
	}
	return -1;  // fd ungültig
}

// System-Call: Nachricht empfangen
int sys_recvmsg(int fd, char *buffer, size_t len) {
	for (int i = 0; i < MAX_QUEUES; i++) {
		if (ipc_queues[i].fd == fd) {
			char received[IPC_MSG_MAX_LEN];
			if (xQueueReceive(ipc_queues[i].queue, received, pdMS_TO_TICKS(2000)) != pdTRUE) {
				return -1;  // Keine Nachricht verfügbar
			}
			strncpy(buffer, received, len);
			buffer[len - 1] = '\0';
			//printf("Nachricht empfangen über Queue %d: %s\n", i, buffer);
			return 0;
		}
	}
	return -1;  // fd ungültig
}

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

void sys_led(int value) {
	xSemaphoreTake(SysLedMutex, portMAX_DELAY);
	gpio_set_level(BLUE_LED_PIN, value);
	xSemaphoreGive(SysLedMutex);
}

void sys_led_blinker_task(void *pvParameters) {
	uint8_t state = 0;
	while(1) {
		sys_led(state);
		state = !state;
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}

void sys_led_mode(uint8_t mode) {
	SysLedMode = mode;
	if(SysLedMode == SYS_LED_HEARTBEAT) {
		if(SysLed == NULL) {
			xTaskCreatePinnedToCore(&sys_led_blinker_task, "sys_led_blinker_task", 1024, NULL, 2, &SysLed, 0);
		}
	}else if(SysLedMode == SYS_LED_USER) {
		if(SysLed != NULL) {
			vTaskDelete(SysLed);
			SysLed = NULL;
		}
	}
}

int fsize(FILE *file) {
	fseek(file, 0, SEEK_END);
	int size = ftell(file);
	fseek(file, 0, SEEK_SET);
	return size;
}

uint16_t getAppsRunning() {
	return AppCount;
}

void close_app(uint8_t current_count) {
	uint8_t timeout_cnt = 0;
	// Beende die App
	if(Apps[current_count].id > -1 && Apps[current_count].stderror > -1) {
		// Sende Nachricht an die App, dass sie beendet wird
		sys_sendmsg(Apps[current_count].id, "exit", 4);
		// Warte auf Bestätigung der App
		char buffer[IPC_MSG_MAX_LEN];
		memset(buffer, 0, IPC_MSG_MAX_LEN);
		while(sys_recvmsg(Apps[current_count].stderror, &buffer, IPC_MSG_MAX_LEN) != 0 && timeout_cnt < 5) {
			timeout_cnt++;
			//printf("Timeout: %d\n", timeout_cnt);
		}
		printf("App %s: %s\n", Apps[current_count].name, buffer);
	} else {
		printf("App %s: Keine Queue vorhanden\n", Apps[current_count].name);
	}
	// Bereinigung und Freigabe von ELF-Ressourcen
	esp_elf_deinit(&Apps[current_count].elf);
	
	// Freigeben des zugewiesenen Speichers für den Code der App
	heap_caps_free(Apps[current_count].exec_mem);
	
	// Markiere die App als 'nicht mehr laufend'
	Apps[current_count].running = 0;
	
	// Logge, dass die App beendet wurde
	ESP_LOGI(TAG, "App %s beendet", Apps[current_count].name);
	
	// Freigabe des Namens-Speichers der App
	heap_caps_free(Apps[current_count].name);
	
	// Schließe die Queues (stdin und stderr)
	sys_closequeue(Apps[current_count].id);
	sys_closequeue(Apps[current_count].stderror);
	
	// Verringere den Zähler für laufende Apps
	AppCount--;
	
	// Lösche den aktuellen Task (da die App beendet wurde)
	vTaskDelete(Apps[current_count].AppHandle);
	Apps[current_count].AppHandle = NULL;
	Apps[current_count].mem_size = 0;
	Apps[current_count].exec_mem = NULL;
}

void start_app() {
	uint8_t current_count = AppStartCount;
		
	//ToDo: App Close function

	// Markiere die App als 'laufend'
	Apps[current_count].running = 1;
	
	// Öffne Queue für stdin (Eingabe)
	Apps[current_count].id = sys_openqueue(Apps[current_count].name);
	if (Apps[current_count].id < 0) {
		ESP_LOGE(TAG, "Fehler beim Öffnen der Eingabe-Queue für %s", Apps[current_count].name);
		close_app(current_count);
		return; // Wenn das Öffnen fehlschlägt, abbrechen
	}
	
	// Öffne Queue für stderr (Fehlerausgabe)
	char stderr_queue_name[MAX_QUEUE_NAME_LEN];
	snprintf(stderr_queue_name, sizeof(stderr_queue_name), "%s_stderr", Apps[current_count].name);
	Apps[current_count].stderror = sys_openqueue(stderr_queue_name);
	if (Apps[current_count].stderror < 0) {
		ESP_LOGE(TAG, "Fehler beim Öffnen der stderr-Queue für %s", Apps[current_count].name);
		sys_closequeue(Apps[current_count].id);  // Schließe die Eingabe-Queue, wenn die Fehler-Queue nicht geöffnet werden konnte
		close_app(current_count);
		return;
	}
	
	// Initialisiere die ELF-Datei
	esp_elf_init(&Apps[current_count].elf);
		
	// Relokation der ELF-Datei (zugehörigen Code im Speicher anpassen)
	esp_elf_relocate(&Apps[current_count].elf, (const uint8_t *)Apps[current_count].exec_mem);
	
	// Anforderung der ELF-Datei (Initialisierung des App-Starts)
	esp_elf_request(&Apps[current_count].elf, 0, 0, NULL);
	close_app(current_count);
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
	//Apps[AppStartCount].name = appname;
	Apps[AppStartCount].name = heap_caps_malloc(strlen(appname) + 1, MALLOC_CAP_SPIRAM);
	strcpy(Apps[AppStartCount].name, appname);
	sprintf(filename, "%s%s%s", APP_PATH, appname, APP_EXT);
	FILE *file = fopen(filename, "rb");
	if (!file) {
		ESP_LOGE(TAG, "Datei %s konnte nicht geöffnet werden!", filename);
		return -1;
	}
	Apps[AppStartCount].mem_size = fsize(file);
	ESP_LOGI(TAG, "%d Bytes an Speicher werden Reserviert", Apps[AppStartCount].mem_size);
	Apps[AppStartCount].exec_mem = heap_caps_malloc(Apps[AppStartCount].mem_size, MALLOC_CAP_SPIRAM);
	fread(Apps[AppStartCount].exec_mem, 1, Apps[AppStartCount].mem_size, file);
	fclose(file);
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
			close_app(i);
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
	}
}

int8_t init_systemcalls() {
	elf_set_custom_symbols(elf_symbols);
	SysLedMutex = xSemaphoreCreateBinary();
	if (SysLedMutex == NULL) {
		ESP_LOGE(TAG, "[APP] Failed to create SysLedMutex semaphore");
		return -1;
	}
	if(xSemaphoreGive(SysLedMutex) != pdTRUE) {
		ESP_LOGE(TAG, "[APP] Failed to give SysLedMutex semaphore");
		return -2;
	}
	gpio_set_direction (BLUE_LED_PIN, GPIO_MODE_OUTPUT);
	if(SysLedMode == SYS_LED_HEARTBEAT) {
		xTaskCreatePinnedToCore(&sys_led_blinker_task, "sys_led_blinker_task", 1024, NULL, 2, &SysLed, 0);
	}
	return 1;
}