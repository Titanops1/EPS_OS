#include "os_commands.h"
#include "shell.h"
#include "wifi.h"
#include "uart_lib.h"
#include "i2c_lib.h"
#include "http_ota.h"
#include "ota_update.h"
#include "systemCalls.h"
#include "pin_def.h"
#include "vga.h"
#include "pong.h"
#include "version.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include <stdio.h>      // Für printf() und perror()
#include <dirent.h> 

#include "../../register_def.h"

#include "test_image.h"

// Struktur zur Verwaltung laufender Apps
typedef struct {
	uint8_t io;              // Input/Output des GPIOs
	uint8_t state;           // State des GPIOs
} GPIO_t;

GPIO_t Gpio[32];

shell_command_t version_command = {
	.command = "version",
	.help = "Zeigt die Version des ESP32 OS",
	.hint = NULL,
	.func = &version_cmd,
};

shell_command_t webserver_command = {
	.command = "webserver",
	.help = "Startet oder stoppt den Webserver",
	.hint = "<start stop>",
	.func = &handle_update_server,
};

shell_command_t update_command = {
	.command = "update",
	.help = "Führt ein Firmware-Update durch.\n-c		Only Check for new Firmware\n-d		Check and Download new Frimware\n-i		Install downloaded Frimware\n",
	.hint = "<-c -d -i>",
	.func = &update,
};

shell_command_t taskList_command = {
	.command = "tasklist",
	.help = "Zeigt eine Liste der Tasks",
	.hint = NULL,
	.func = &get_task_list_cmd,
};

shell_command_t appList_command = {
	.command = "applist",
	.help = "Zeigt eine Liste der Apps",
	.hint = NULL,
	.func = &printAppList,
};

shell_command_t startApp_command = {
	.command = "start",
	.help = "Startet eine App",
	.hint = NULL,
	.func = &startApp_cmd,
};

shell_command_t stopApp_command = {
	.command = "stop",
	.help = "Stoppt eine App",
	.hint = NULL,
	.func = &stopApp_cmd,
};

shell_command_t move_command = {
	.command = "mv",
	.help = "Verschiebt eine Datei",
	.hint = NULL,
	.func = &move_cmd,
};

shell_command_t remove_command = {
	.command = "rm",
	.help = "Löscht eine Datei",
	.hint = NULL,
	.func = &remove_cmd,
};

shell_command_t list_file_command = {
	.command = "ls",
	.help = "Listet Dateien im Verzeichnis auf",
	.hint = NULL,
	.func = &list_file_cmd,
};

shell_command_t print_file_command = {
	.command = "cat",
	.help = "Zeigt den Inhalt einer Datei",
	.hint = NULL,
	.func = &print_file_cmd,
};

shell_command_t free_mem_command = {
	.command = "free",
	.help = "Zeigt den freien Speicher",
	.hint = NULL,
	.func = &free_mem_cmd,
};

shell_command_t wlan_printip_command = {
	.command = "ifconfig",
	.help = "Zeigt die IP-Adresse",
	.hint = NULL,
	.func = &wlan_ip_cmd,
};

shell_command_t wlan_command = {
	.command = "wlan",
	.help = "Steuerung der WLAN-Verbindung",
	.hint = "<disconnect connect reset scan list auto forget> <ssid> <password>",
	.func = &wlan_cmd,
};

shell_command_t reboot_command = {
	.command = "reboot",
	.help = "Startet das System neu",
	.hint = NULL,
	.func = &cmd_reboot,
};

shell_command_t shutdown_command = {
	.command = "shutdown",
	.help = "Fährt das System herunter oder startet es neu",
	.hint = "<-r -h> <time>",
	.func = &cmd_shutdown,
};

shell_command_t interface_command = {
	.command = "if",
	.help = "Konfiguriert die Netzwerkschnittstelle",
	.hint = "<up down>",
	.func = &interface_cmd,
};

shell_command_t debug_command = {
	.command = "debug",
	.help = "Konfiguriert die Debug-Ausgabe",
	.hint = "<None Error Warnung Info Debug All>",
	.func = &debug_cmd,
};

shell_command_t gpio_command = {
	.command = "gpio",
	.help = "Steuert die gpios",
	.hint = "<set get> <Pin IO State>",
	.func = &gpio_cmd,
};

shell_command_t vga_command = {
	.command = "vga",
	.help = "Steuert die VGA Schnittstelle",
	.hint = "Usage: vga <clear line rect fillrect text circle fillcircle triangle filltriangle> <x y x1 y1 x2 y2> <r g b> <text>",
	.func = &vga_cmd,
};

// Handler für den "version"-Befehl
int version_cmd(int argc, char **argv) {
	printf("ESP32 OS Version: %s\n", OS_VERSION);
	return 0;
}

int handle_update_server(int argc, char **argv) {
	if(argc < 2) {
		printf("Usage: webserver <start stop>\n");
		return 1;
	}
	if(strcmp(argv[1], "start") == 0) {
		start_webserver();
	} else if(strcmp(argv[1], "stop") == 0) {
		stop_webserver();
	}
	return 0;
}

int update(int argc, char **argv) {
	int update_arg = 0;
	if(argc == 1)
	{ //check, download and install new firmware
		update_arg = 0;
	}
	else
	{
		if(strcmp(argv[1], "-c") == 0)
		{ //only check
			update_arg = 1;
		}
		else if(strcmp(argv[1], "-d") == 0)
		{ //only check download
			update_arg = 2;
		}
		else if(strcmp(argv[1], "-i") == 0)
		{ //only install downloaded file
			update_arg = 3;
		}
	}
	check_and_update_firmware(update_arg);
	return 0;
}

int get_task_list_cmd(int argc, char **argv) {
	TaskStatus_t task_list[20];  // Platz für 20 Tasks
	UBaseType_t task_count, i;
	uint64_t total_runtime;

	task_count = uxTaskGetSystemState(task_list, 20, &total_runtime);

	printf("Tasks aktiv: %d davon %d App\n", uxTaskGetNumberOfTasks(), getAppsRunning());
	printf("Task Name       State      Prio  Stack  Core  CPU\n");
	printf("--------------------------------------------------------\n");
	for (i = 0; i < task_count; i++) {
		float usage = (total_runtime > 0)
            ? (100.0f * task_list[i].ulRunTimeCounter / total_runtime)
            : 0.0f;

		const char *state;
		switch (task_list[i].eCurrentState) {
			case eRunning:   state = "Running"; break;
			case eReady:     state = "Ready"; break;
			case eBlocked:   state = "Blocked"; break;
			case eSuspended: state = "Suspended"; break;
			case eDeleted:   state = "Deleted"; break;
			default:         state = "Unknown"; break;
		}

		//int core_id = xTaskGetAffinity(task_list[i].xHandle); // Kern-ID abrufen

		printf("%-15s %-10s %2d    %6ld %4d  %5.1f%%\n",
			   task_list[i].pcTaskName,
			   state,
			   task_list[i].uxCurrentPriority,
			   task_list[i].usStackHighWaterMark,
			   task_list[i].xCoreID,
			   usage);
	}
	return 0;
}

int startApp_cmd(int argc, char **argv) {
	if(argc < 2) {
		printf("Usage: start <appname>\n");
		return 1;
	}
	registerApp(argv[1]);
	return 0;
}

int stopApp_cmd(int argc, char **argv) {
	if(argc < 2) {
		printf("Usage: stop <appname>\n");
		return 1;
	}
	unregisterApp(argv[1]);
	return 0;
}

int move_cmd(int argc, char **argv) {
	if(argc < 3) {
		printf("Usage: mv <src> <dst>\n");
		return 1;
	}
	remove(argv[2]);
	rename(argv[1], argv[2]);
	remove(argv[1]);
	return 0;
}

int remove_cmd(int argc, char **argv) {
	if(argc < 2) {
		printf("Usage: rm <appname>\n");
		return 1;
	}
	remove(argv[1]);
	return 0;
}

int list_file_cmd(int argc, char **argv) {
	if(argc < 2) {
		printf("Usage: ls <path>\n");
		return 1;
	}
	DIR *dir;
	struct dirent *ent;
	if ((dir = opendir(argv[1])) != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			printf("%s\n", ent->d_name);
		}
		closedir(dir);
	} else {
		perror("Fehler beim Öffnen des Verzeichnisses");
		return 1;
	}
	return 0;
}

int print_file_cmd(int argc, char **argv) {
	if(argc < 2) {
		printf("Usage: cat <filename>\n");
		return 1;
	}
	const char *filename = argv[1];
	FILE *file = fopen(filename, "r");
	if(file == NULL) {
		printf("Datei nicht gefunden\n");
		return 1;
	}
	char line[128];
	while(fgets(line, sizeof(line), file)) {
		printf("%s", line);
	}
	fclose(file);
	return 0;
}

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
	printMemorySize(heap_caps_get_total_size(MALLOC_CAP_8BIT));
	printf("\n");
	printf("Freier Heap:      ");
	printMemorySize(heap_caps_get_free_size(MALLOC_CAP_8BIT));
	printf("\n");
	printf("Größter Block:    ");
	printMemorySize(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
	printf("\n");
	printf("Interner Heap:    ");
	printMemorySize(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
	printf("\n");
	printf("Externer Heap:    ");
	printMemorySize(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
	printf("\n");
}

int free_mem_cmd(int argc, char **argv) {
	printf("Free Memory:\n");
	print_heap_info();
	// printMemorySize(esp_get_free_heap_size());
	// printf("\n");
	return 0;
}

int wlan_cmd(int argc, char **argv) {
	if(argc < 2) {
		printf("Usage: wlan <disconnect connect reset scan list auto forget> <ssid> <password>\n");
		return 1;
	}
	if(strcmp(argv[1], "disconnect") == 0) {
		wlan_disconnect();
	} else if(strcmp(argv[1], "connect") == 0) {
		EventBits_t bits;
		if(argc == 3) {
			char *password[64];
			int ret = load_wifi_credentials(argv[2], &password);
			if(ret == 1) {
				bits = wlan_connect(argv[2], (char *)password);
				return 0;
			}else {
				printf("Try: wlan connect <ssid> <password>\n");
			}
			return 0;
		}
		if(argc < 4) {
			printf("Usage: wlan connect <ssid> <password>\n");
			return 1;
		}
		bits = wlan_connect(argv[2], argv[3]);
		if(bits & WIFI_CONNECTED_BIT) {
			ESP_LOGI("WIFI", "Speichere SSID: %s", argv[1]);
			save_wifi_credentials(argv[2], argv[3]);
		}else {
			ESP_LOGI("WIFI", "WI-FI-Verbindung fehlgeschlagen mit Fehler %ld", bits);
		}
	} else if(strcmp(argv[1], "reset") == 0) {
		delete_wifi_config();
	} else if(strcmp(argv[1], "scan") == 0) {
		cmd_wifi_scan(argc, argv);
	} else if(strcmp(argv[1], "list") == 0) {
		list_wifi_credentials();
	} else if(strcmp(argv[1], "auto") == 0) {
		connect_saved_wifi();
	} else if(strcmp(argv[1], "forget") == 0) {
		if(argc < 3) {
			printf("Usage: wlan forget <ssid>\n");
			return 1;
		}
		cmd_wifi_forget(argc, argv);
	}
	return 0;
}

int wlan_ip_cmd(int argc, char **argv) {
	print_ip_address();
	return 0;
}

int cmd_wifi_scan(int argc, char **argv) {
	wifi_scan();
	print_wifi_scan();
	return 0;
}

int cmd_wifi_auto(int argc, char **argv) {
	connect_saved_wifi();
	return 0;
}

int cmd_wifi_forget(int argc, char **argv) {
	char *password;
	uint8_t passed = load_wifi_credentials(argv[1], &password);

	if (passed == 1) {
		reset_wifi_credentials(argv[1]);
		ESP_LOGI("WIFI", "Netzwerk '%s' wurde vergessen.", argv[1]);
	} else {
		ESP_LOGI("WIFI", "Netzwerk '%s' nicht gefunden.", argv[1]);
	}
	return 0;
}

int cmd_reboot(int argc, char **argv) {
	uint16_t buf[3];
	buf[0] = CMD_REBOOT; //reboot
	sendRPi(REG_POWER, buf, 1);
	delay_ms(10);
	printf("System wird neu gestartet\n");
	esp_restart();
	return 0;
}

int cmd_shutdown(int argc, char **argv) {
	uint16_t buf[3];

	if(argc < 3) {
		printf("Usage: shutdown <-r -h> <time>\n");
		return 1;
	}
	if(strcmp(argv[1], "-r") == 0) {
		buf[0] = CMD_REBOOT; //reboot
		sendRPi(REG_POWER, buf, 1);
		if(strcmp(argv[2], "now") == 0) {
			printf("System wird neu gestartet\n");
			delay_ms(10);
			esp_restart();
		}else {
			printf("System wird in %d Sekunden neu gestartet\n", atoi(argv[2]));
			delay_ms(10);
			vTaskDelay(pdMS_TO_TICKS(atoi(argv[2])*1000));
			esp_restart();
		}
	} else if(strcmp(argv[1], "-h") == 0) {
		buf[0] = CMD_SHUTDOWN; //shutdown
		sendRPi(REG_POWER, buf, 1);
		if(strcmp(argv[2], "now") == 0) {
			printf("System wird heruntergefahren\n");
			//gpio_set_level(RP2040_RST_PIN, 0);
			delay_ms(10);
			esp_sleep_enable_timer_wakeup((uint64_t)(281474976710656)); // ~8,9 Jahre
			esp_deep_sleep_start();
		}else {
			printf("System wird in %d Sekunden heruntergefahren\n", atoi(argv[2]));
			vTaskDelay(pdMS_TO_TICKS(atoi(argv[2])*1000));
			//gpio_set_level(RP2040_RST_PIN, 0);
			delay_ms(10);
			esp_sleep_enable_timer_wakeup((uint64_t)(281474976710656)); // ~8,9 Jahre
			esp_deep_sleep_start();
		}
	}
	return 0;
}

int interface_cmd(int argc, char **argv) {
	if(argc < 2) {
		printf("Usage: if <up down>\n");
		return 1;
	}
	if(strcmp(argv[1], "up") == 0) {
		start_wifi();
	} else if(strcmp(argv[1], "down") == 0) {
		stop_wifi();
	}
	return 0;
}

int debug_cmd(int argc, char **argv)
{
	if(argc < 2) {
		printf("Usage: debug <None Error Warnung Info Debug All>\n");
		return 1;
	}

	if(strcmp(argv[1], "None") == 0)
	{
		esp_log_level_set("*", ESP_LOG_NONE);
	}
	else if(strcmp(argv[1], "Error") == 0)
	{
		esp_log_level_set("*", ESP_LOG_ERROR);
	}
	else if(strcmp(argv[1], "Warnung") == 0)
	{
		esp_log_level_set("*", ESP_LOG_WARN);
	}
	else if(strcmp(argv[1], "Info") == 0)
	{
		esp_log_level_set("*", ESP_LOG_INFO);
	}
	else if(strcmp(argv[1], "Debug") == 0)
	{
		esp_log_level_set("*", ESP_LOG_DEBUG);
	}
	else if(strcmp(argv[1], "All") == 0)
	{
		esp_log_level_set("*", ESP_LOG_VERBOSE);
	}
	else
	{
		return 2;
	}
	return 0;
}

int gpio_cmd(int argc, char **argv)
{
	uint16_t timeout_cnt = 0;
	if(argc < 2) {
		printf("Usage: gpio <set get> Pin IO State\n");
		return 1;
	}

	if(strcmp(argv[1], "set") == 0)
	{
		uint16_t buf[4];
		uint8_t pin = atoi(argv[2]);
		Gpio[pin].io = atoi(argv[3]);
		Gpio[pin].state = atoi(argv[4]);
		buf[0] = 1; //Set GPIO
		buf[1] = pin;
		buf[2] = Gpio[pin].io;
		buf[3] = Gpio[pin].state;
		sendRPi(REG_GPIO, buf, 4);
	}
	else if(strcmp(argv[1], "get") == 0)
	{
		uint16_t buf[2];
		uint8_t pin = atoi(argv[2]);
		buf[0] = 0; //Get GPIO
		buf[1] = pin;
		sendRPi(REG_GPIO, buf, 2);
		while(getRxComplete() != 1 && timeout_cnt < 1000)
		{
			timeout_cnt++;
			delay_ms(10);
		}
		if(getRxComplete() == 1)
		{
			if(getRxReg() == REG_GPIO && getRxData(0) == 0)
			{
				printf("GPIO%d: %d\n", pin, getRxData(1));
				clearRxComplete();
			}
		}
		else
		{
			printf("No Data received\n");
		}
	}
	else
	{
		return 2;
	}
	return 0;
}

int vga_cmd(int argc, char **argv)
{
	if(argc < 2) {
		printf("Usage: vga <clear line rect fillrect text circle fillcircle triangle filltriangle> <x y x1 y1 x2 y2> <r g b> <text>\n");
		return 1;
	}

	if(strcmp(argv[1], "clear") == 0)
	{
		if(argc < 5)
		{
			printf("vga clear <r g b>\n");
			return 1;
		}
		uint8_t r = atoi(argv[2]);
		uint8_t g = atoi(argv[3]);
		uint8_t b = atoi(argv[4]);
		vga_clear_screen(r, g, b);
	}
	else if(strcmp(argv[1], "line") == 0)
	{
		if(argc < 9)
		{
			printf("vga line <x0 y0 x1 y1> <r g b>\n");
			return 1;
		}
		uint16_t x0 = atoi(argv[2]);
		uint16_t y0 = atoi(argv[3]);
		uint16_t x1 = atoi(argv[4]);
		uint16_t y1 = atoi(argv[5]);
		uint8_t r = atoi(argv[6]);
		uint8_t g = atoi(argv[7]);
		uint8_t b = atoi(argv[8]);
		vga_draw_line(x0, y0, x1, y1, r, g, b);
	}
	else if(strcmp(argv[1], "rect") == 0)
	{
		if(argc < 9)
		{
			printf("vga rect <x y w h> <r g b>\n");
			return 1;
		}
		uint16_t x0 = atoi(argv[2]);
		uint16_t y0 = atoi(argv[3]);
		uint16_t w = atoi(argv[4]);
		uint16_t h = atoi(argv[5]);
		uint8_t r = atoi(argv[6]);
		uint8_t g = atoi(argv[7]);
		uint8_t b = atoi(argv[8]);
		vga_draw_rect(x0, y0, w, h, r, g, b);
	}
	else if(strcmp(argv[1], "fillrect") == 0)
	{
		if(argc < 9)
		{
			printf("vga fillrect <x y w h> <r g b>\n");
			return 1;
		}
		uint16_t x0 = atoi(argv[2]);
		uint16_t y0 = atoi(argv[3]);
		uint16_t x1 = atoi(argv[4]);
		uint16_t y1 = atoi(argv[5]);
		uint8_t r = atoi(argv[6]);
		uint8_t g = atoi(argv[7]);
		uint8_t b = atoi(argv[8]);
		vga_fill_rect(x0, y0, x1, y1, r, g, b);
	}
	else if(strcmp(argv[1], "text") == 0)
	{
		if(argc < 8)
		{
			printf("vga text <x y> <r g b>  <text>\n");
			return 1;
		}
		uint16_t x0 = atoi(argv[2]);
		uint16_t y0 = atoi(argv[3]);
		uint8_t r = atoi(argv[4]);
		uint8_t g = atoi(argv[5]);
		uint8_t b = atoi(argv[6]);
		char *text = argv[7];
		vga_draw_text(x0, y0, text, r, g, b);
	}
	else if(strcmp(argv[1], "circle") == 0)
	{
		if(argc < 8)
		{
			printf("vga circle <x y r> <r g b>\n");
			return 1;
		}
		uint16_t x = atoi(argv[2]);
		uint16_t y = atoi(argv[3]);
		uint16_t radius = atoi(argv[4]);
		uint8_t r = atoi(argv[5]);
		uint8_t g = atoi(argv[6]);
		uint8_t b = atoi(argv[7]);
		vga_draw_circle(x, y, radius, r, g, b);
	}
	else if(strcmp(argv[1], "fillcircle") == 0)
	{
		if(argc < 8)
		{
			printf("vga fillcircle <x y r> <r g b>\n");
			return 1;
		}
		uint16_t x = atoi(argv[2]);
		uint16_t y = atoi(argv[3]);
		uint16_t radius = atoi(argv[4]);
		uint8_t r = atoi(argv[5]);
		uint8_t g = atoi(argv[6]);
		uint8_t b = atoi(argv[7]);
		vga_fill_circle(x, y, radius, r, g, b);
	}
	else if(strcmp(argv[1], "triangle") == 0)
	{
		if(argc < 11)
		{
			printf("vga triangle <x0 y0 x1 y1 x2 y2> <r g b>\n");
			return 1;
		}
		uint16_t x0 = atoi(argv[2]);
		uint16_t y0 = atoi(argv[3]);
		uint16_t x1 = atoi(argv[4]);
		uint16_t y1 = atoi(argv[5]);
		uint16_t x2 = atoi(argv[6]);
		uint16_t y2 = atoi(argv[7]);
		uint8_t r = atoi(argv[8]);
		uint8_t g = atoi(argv[9]);
		uint8_t b = atoi(argv[10]);
		vga_draw_triangle(x0, y0, x1, y1, x2, y2, r, g, b);
	}
	else if(strcmp(argv[1], "filltriangle") == 0)
	{
		if(argc < 11)
		{
			printf("vga filltriangle <x0 y0 x1 y1 x2 y2> <r g b>\n");
			return 1;
		}
		uint16_t x0 = atoi(argv[2]);
		uint16_t y0 = atoi(argv[3]);
		uint16_t x1 = atoi(argv[4]);
		uint16_t y1 = atoi(argv[5]);
		uint16_t x2 = atoi(argv[6]);
		uint16_t y2 = atoi(argv[7]);
		uint8_t r = atoi(argv[8]);
		uint8_t g = atoi(argv[9]);
		uint8_t b = atoi(argv[10]);
		vga_fill_triangle(x0, y0, x1, y1, x2, y2, r, g, b);
	}
	else if(strcmp(argv[1], "graphictest") == 0)
	{
		run_graphics_test();
	}
	else if(strcmp(argv[1], "getwindow") == 0)
	{
		vga_get_frame_size();
		printf("Window Size: %dx%d\n", vga_getWindowWidth(), vga_getWindowHeigth());
	}
	else if(strcmp(argv[1], "swap") == 0)
	{
		vga_swap_buffers();
	}
	else if(strcmp(argv[1], "pong") == 0)
	{
		pong_run();
	}
	else if(strcmp(argv[1], "scroll") == 0)
	{
		if(argc < 4)
		{
			printf("vga scroll <x y>\n");
			return 1;
		}
		uint16_t x0 = atoi(argv[2]);
		uint16_t y0 = atoi(argv[3]);
		vga_scroll(x0, y0);
	}
	else if(strcmp(argv[1], "load") == 0)
	{
		#define MAX_SEND_IMG 120

		uint16_t width  = vga_getWindowWidth();
		uint16_t height = vga_getWindowHeigth();

		for (uint16_t y = 0; y < height; y++) {
			uint16_t x = 0;
			while (x < width) {
				uint16_t remaining = width - x;
				uint16_t chunk = (remaining > MAX_SEND_IMG) ? MAX_SEND_IMG : remaining;

				// Sende diesen Block
				vga_load_image_data(x, y, &img_buffer[y * width + x], chunk);

				// Debug optional
				// printf("Y:%3d X:%3d Chunk:%3d Rem:%3d\n", y, x, chunk, remaining);
				// fflush(stdout);

				x += chunk;
				// Optional kleine Pause, falls UART puffert:
				// vTaskDelay(pdMS_TO_TICKS(1));
			}
		}
		vga_swap_buffers();
	}
	else
	{
		return 2;
	}
	//vga_swap_copie_buffers();
	return 0;
}

void register_commands(void)
{
	//Register OS Commands
	//define in os_commands.h
	shell_register(&version_command);
	shell_register(&webserver_command);
	shell_register(&update_command);
	shell_register(&taskList_command);
	shell_register(&appList_command);
	shell_register(&startApp_command);
	shell_register(&stopApp_command);
	shell_register(&move_command);
	shell_register(&remove_command);
	shell_register(&list_file_command);
	shell_register(&print_file_command);
	shell_register(&free_mem_command);
	shell_register(&wlan_command);
	shell_register(&wlan_printip_command);
	shell_register(&reboot_command);
	shell_register(&shutdown_command);
	shell_register(&interface_command);
	shell_register(&debug_command);
	shell_register(&gpio_command);
	shell_register(&vga_command);
}
