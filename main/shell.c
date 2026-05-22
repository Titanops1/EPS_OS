#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "memory.h"
#include "systemCalls.h"
#include "swi.h"
#include "shell.h"
#include "window_manager.h"
#include "vga.h"

#define UART_PORT_NUM		UART_NUM_0
#define UART_BAUD_RATE		115200
#define UART_BUF_SIZE		1024

#define MAX_INPUT_LEN		255
#define MAX_ARGS			25

#define MAX_COMMANDS		128

#define HISTORY_LEN			10

static char line[MAX_INPUT_LEN];
static int pos = 0;
static int line_len = 0;
static char history[HISTORY_LEN][MAX_INPUT_LEN];
static int history_count = 0;
static int history_index = -1;

static const char *TAG = "SHELL";

// Forward declarations
static int cmd_help(int argc, char **argv);
static int cmd_echo(int argc, char **argv);

static shell_command_t **commands = NULL;
static int num_commands = 0;
int shell_id = -1;
swi_app_id_t shell_swi_id;
swi_app_id_t cmd_id;

TaskHandle_t cmdTask;

static void shell_on_focus_gained(void) {
	// vga_clear_screen(0, 0, 0);
	// setCursor(10, 2);
	// drawText("esp os> ", 0, 255, 0, 255);
	// vga_swap_buffers();
	printf("\nesp os> ");
	fflush(stdout);
}

static void shell_on_focus_lost(void) {
	printf("\n[Shell paused]\n");
}

// ---------- UART Setup ----------
static void uart_init_shell(void) {
	uart_config_t uart_config = {
		.baud_rate = UART_BAUD_RATE,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};
	ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
	ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE, 0, 0, NULL, 0));
}


int shell_register(const shell_command_t *cmd) {
	shell_command_t *new_cmd = psram_malloc(sizeof(shell_command_t));
	if (!new_cmd) return -1;

	new_cmd->command = strdup(cmd->command);
	new_cmd->help    = strdup(cmd->help);
	new_cmd->hint    = cmd->hint ? strdup(cmd->hint) : NULL;
	new_cmd->func    = cmd->func;

	// Array vergrößern
	shell_command_t **tmp = psram_realloc(commands, (num_commands + 1) * sizeof(shell_command_t *));
	if (!tmp) {
		psram_free(new_cmd);
		return -1;
	}

	commands = tmp;
	commands[num_commands] = new_cmd;
	num_commands++;
	return 0;
}

int shell_unregister(const char *command) {
	if (!command || num_commands == 0) {
		return -1;
	}

	for (int i = 0; i < num_commands; i++) {
		shell_command_t *cmd = commands[i];

		if (strcmp(cmd->command, command) == 0) {
			// Speicher freigeben
			psram_free(&cmd->command);
			psram_free(&cmd->help);
			if (cmd->hint) psram_free(&cmd->hint);
			psram_free(&cmd);

			// Array neu organisieren
			for (int j = i; j < num_commands - 1; j++) {
				commands[j] = commands[j + 1];
			}
			num_commands--;

			// Array verkleinern
			if (num_commands == 0) {
				psram_free(commands);
				commands = NULL;
			} else {
				shell_command_t **tmp = heap_caps_realloc(commands, num_commands * sizeof(shell_command_t *), MALLOC_CAP_SPIRAM);
				if (tmp) {
					commands = tmp;
				}
			}

			return 0; // erfolgreich deregistriert
		}
	}

	return -1; // Command nicht gefunden
}

// ---------- Input Parser ----------
static void parse_and_execute(char *line) {
	char *argv[MAX_ARGS];
	int argc = 0;

	// Tokenize input
	char *token = strtok(line, " \t\n\r");
	while (token != NULL && argc < MAX_ARGS) {
		argv[argc++] = token;
		token = strtok(NULL, " \t\n\r");
	}
	if (argc == 0)
	{
		swi_unregister_app(cmd_id);
		cmdTask = NULL;
		vTaskDelete(NULL);
		return;
	}

	// Find command
	for (int i = 0; i < num_commands; i++) {
		if (strcmp(argv[0], commands[i]->command) == 0) {
			commands[i]->func(argc, argv);
			printf("\nesp os> ");
			fflush(stdout);
			// if(focus_has(shell_id))
			// {
			// 	drawText("esp os> ", 0, 255, 0, 255);
			// 	vga_swap_buffers();
			// }
			swi_unregister_app(cmd_id);
			cmdTask = NULL;
			vTaskDelete(NULL);
			return;
		}
	}

	printf("Unknown command: %s\n", argv[0]);
	printf("\nesp os> ");
	fflush(stdout);
	// if(focus_has(shell_id))
	// {
	// 	drawText("esp os> ", 0, 255, 0, 255);
	// 	vga_swap_buffers();
	// }
	swi_unregister_app(cmd_id);
	cmdTask = NULL;
	vTaskDelete(NULL);
}

// ---------- Command Implementations ----------
static int cmd_help(int argc, char **argv) {
	printf("Available commands:\n");
	for (int i = 0; i < num_commands; i++) {
		printf("  %-8s - %s\n", commands[i]->command, commands[i]->help);
	}
	return 0;
}

static int cmd_echo(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		printf("%s ", argv[i]);
	}
	printf("\n");
	return 0;
}

// ---------- Shell Task ----------
static void shell_task(void *arg) {
	uint8_t data;
	int esc_state = 0;

	shell_swi_id = swi_get_appId();

	// shell_id = window_register("shell", xTaskGetCurrentTaskHandle(), shell_on_focus_gained, shell_on_focus_lost);

	// window_set_priority(shell_id, MAX_WINDOW_PRIORITY-1);
	// focus_request(shell_id);

	printf("\nWelcome to ESP32 Shell! Type 'help' to see commands.\n> ");
	printf("esp os> ");
	// drawText("Welcome to ESP32 Shell! Type 'help' to see commands.", 255, 255, 255, 255);
	// setCursorNextLine();
	// drawText("esp os> ", 0, 255, 0, 255);
	// vga_swap_buffers();

	while (1) {
		int len = uart_read_bytes(UART_PORT_NUM, &data, 1, portMAX_DELAY);
		if (len <= 0) continue;

		if (esc_state == 0) {
			if (data == '\r' || data == '\n') {
				printf("\n");
				line[line_len] = '\0';
				if (line_len > 0) {
					BaseType_t res = xTaskCreate(
						parse_and_execute, "cmd", 4096,
						(void*)line,
						tskIDLE_PRIORITY + 1,
						&cmdTask
					);
					cmd_id = swi_register_app(cmdTask, 32);

					// In History speichern
					if (history_count < HISTORY_LEN) {
						strcpy(history[history_count++], line);
					} else {
						for (int i = 1; i < HISTORY_LEN; i++) {
							strcpy(history[i-1], history[i]);
						}
						strcpy(history[HISTORY_LEN-1], line);
					}
				}
				pos = 0;
				line_len = 0;
				setCursorNextLine();
				// printf("esp os> ");
				// fflush(stdout);

			} else if (data == 0x08 || data == 0x7F) {  // Backspace
				if (pos > 0) {
					memmove(&line[pos-1], &line[pos], line_len - pos);
					pos--;
					line_len--;
					printf("\b \b"); // Bildschirm löschen
					fflush(stdout);
					// if(focus_has(shell_id))
					// {
					// 	setCursor(getXCursor()-getXFontSize(), getYCursor());
					// 	drawText(" ", 255, 255, 255, 255);
					// 	setCursor(getXCursor()-getXFontSize(), getYCursor());
					// 	vga_swap_buffers();
					// }
				}

			} else if (data == 0x1B) { // ESC
				esc_state = 1;
				ESP_LOGD(TAG, "ESC wurde empfangen.");

			} else if (data == 0x03) { // Ctrl+C
				printf("^C\n");
				swi_msg_t m = {0};
				m.type = 0;
				m.data[0] = 9;
				swi_send_message(cmd_id, &m, 0); // 0 ticks wait
				uint8_t timeout_loop = 0;
				while(timeout_loop < 200 && cmdTask != NULL)
				{
					vTaskDelay(pdMS_TO_TICKS(100));
					timeout_loop++;
				}
				printf("Timeout: %d\n", timeout_loop);
				// vTaskDelay(pdMS_TO_TICKS(20000));
				if (cmdTask != NULL) {
					printf("Delete Task\n");
					vTaskDelete(cmdTask);
					swi_unregister_app(cmd_id);
					cmdTask = NULL;
				}
				pos = 0;
				line_len = 0;
				printf("esp os> ");
				fflush(stdout);
				// if(focus_has(shell_id))
				// {
				// 	drawText("^C", 255, 255, 255, 255);
				// 	setCursorNextLine();
				// 	drawText("esp os> ", 0, 255, 0, 255);
				// 	vga_swap_buffers();
				// }

			} else if (data >= 0x20 && data <= 0x7E && line_len < MAX_INPUT_LEN - 1) {
				// Zeichen einfügen
				memmove(&line[pos+1], &line[pos], line_len - pos);
				line[pos] = data;
				pos++;
				line_len++;
				uart_write_bytes(UART_PORT_NUM, (const char *)&data, 1);
				// if(focus_has(shell_id))
				// {
				// 	drawText((const char *)&data, 255, 255, 255, 255);
				// 	vga_swap_buffers();
				// }
				// else
				// {
				// 	printf("Shell has no Focus\n");
				// 	if(focus_request(shell_id))
				// 	{
				// 		drawText((const char *)&data, 255, 255, 255, 255);
				// 		vga_swap_buffers();
				// 	}
				// }
			}

		} else if (esc_state == 1) {
			if (data == '[') {
				esc_state = 2; // warten auf den nächsten
			} else {
				esc_state = 0;
			}

		} else if (esc_state == 2) {
			if (data == 'D') { // Links
				ESP_LOGD(TAG, "Pfeiltaste Links.");
				if (pos > 0) {
					printf("\b");
					pos--;
				}
			} else if (data == 'C') { // Rechts
				ESP_LOGD(TAG, "Pfeiltaste Rechts.");
				if (pos < line_len) {
					uart_write_bytes(UART_PORT_NUM, &line[pos], 1);
					pos++;
				}
			} else if (data == 'A') { // Hoch = History zurück
				ESP_LOGD(TAG, "Pfeiltaste Hoch.");
				if (history_count > 0) {
					if (history_index < history_count - 1) history_index++;
					strcpy(line, history[history_count - 1 - history_index]);
					line_len = strlen(line);
					pos = line_len;
					printf("\r\033[Kesp os> %s", line);
					fflush(stdout);
				}
			} else if (data == 'B') { // Runter = History vor
				ESP_LOGD(TAG, "Pfeiltaste Runter.");
				if (history_index > 0) {
					history_index--;
					strcpy(line, history[history_count - 1 - history_index]);
				} else {
					history_index = -1;
					line[0] = '\0';
				}
				line_len = strlen(line);
				pos = line_len;
				printf("\r\033[Kesp os> %s", line);
				fflush(stdout);
			}
			esc_state = 0;
		}
	}
}

int shell_getId()
{
	return shell_swi_id;
}

// ---------- Main ----------
void shell_init(void) {
	shell_command_t help_command = {
		.command = "help",
		.help = "Zeigt die Hilfe an",
		.hint = NULL,
		.func = &cmd_help,
	};
	shell_command_t echo_command = {
		.command = "echo",
		.help = "Echo alles was nach dem befehl eingefügt wurde",
		.hint = NULL,
		.func = &cmd_echo,
	};
	shell_register(&help_command);
	shell_register(&echo_command);
	uart_init_shell();
	registerSysApp(shell_task, "shell_task");
	//xTaskCreate(shell_task, "shell_task", 4096, NULL, 5, NULL);
}