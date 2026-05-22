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
#include <math.h>
#include "memory.h"
#include <dirent.h> 

#include "explorer.h"
#include "os_commands.h"
#include "vga.h"
#include "window_manager.h"
#include "systemCalls.h"

#define APP_PATH "/spiffs/application"

#define BUTTON_WIDTH   80
#define BUTTON_HEIGHT  30
#define BUTTON_MARGIN  10
#define BUTTONS_PER_ROW 3

// Struktur für ein Kommando
typedef struct {
	uint16_t app_cnt;

} app_list_t;

void explorer_app_clicked(widget_t *w)
{
	button_t *btn = (button_t*)w;

	printf("Starte App: %s\n", btn->text);

	// hier deine echte Startfunktion
	registerApp(btn->text);
}

void init_explorer(uint16_t win_width, uint16_t win_height)
{
	window_t* explorer_win;

	explorer_win = createForm(
		0,
		0,
		win_width,
		win_height,
		0
	);

	DIR *dir;
	struct dirent *ent;

	uint16_t index = 0;

	if ((dir = opendir(APP_PATH)) != NULL) {

		while ((ent = readdir(dir)) != NULL) {

			// "." und ".." ignorieren
			if (ent->d_name[0] == '.')
				continue;


			// Nur .elf anzeigen
			char *ext = strstr(ent->d_name, ".elf");

			if (!ext || strcmp(ext, ".elf") != 0)
				continue;

			// Name kopieren ohne .elf
			char name[128];
			strncpy(name, ent->d_name, sizeof(name));
			name[sizeof(name) - 1] = '\0';
			// ".elf" abschneiden
			name[ext - ent->d_name] = '\0';
			//--------------------------------
			// Position berechnen
			//--------------------------------

			uint16_t row = index / BUTTONS_PER_ROW;
			uint16_t col = index % BUTTONS_PER_ROW;

			uint16_t x =
				BUTTON_MARGIN +
				col * (BUTTON_WIDTH + BUTTON_MARGIN);

			uint16_t y =
				BUTTON_MARGIN +
				row * (BUTTON_HEIGHT + BUTTON_MARGIN);

			//--------------------------------
			// Button erstellen
			//--------------------------------

			button_t *btn =
				createButton(
					x,
					y,
					BUTTON_WIDTH,
					BUTTON_HEIGHT,
					name
				);
			printf("APP Name %s\n", name);
			//--------------------------------
			// Callback setzen
			//--------------------------------

			btn->on_click = explorer_app_clicked;

			//--------------------------------
			// Widget hinzufügen
			//--------------------------------

			window_add_widget(
				explorer_win,
				(widget_t*)btn
			);

			index++;
		}

		closedir(dir);

	} else {

		perror("Fehler beim Öffnen des Verzeichnisses");
	}
}