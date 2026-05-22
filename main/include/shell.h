#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>
#include "swi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Funktionspointer für Befehle
typedef int (*shell_cmd_func_t)(int argc, char **argv);

// Struktur für ein Kommando
typedef struct {
	const char *command;      // Name des Befehls (z. B. "echo")
	const char *help;         // Hilfe-/Beschreibungstext
	const char *hint;         // Hilfe-/Beschreibungstext
	shell_cmd_func_t func;    // Funktionszeiger
} shell_command_t;

int shell_getId();

/**
 * @brief Initialisiert die Shell (UART, Tasks etc.)
 */
void shell_init(void);

/**
 * @brief Registriert einen neuen Befehl in der Shell.
 * 
 * @param cmd Pointer auf eine shell_command_t-Struktur
 * @return 0 bei Erfolg, <0 bei Fehler
 */
int shell_register(const shell_command_t *cmd);

/**
 * @brief Gibt einen  Befehl in der Shell frei.
 * 
 * @param cmd Pointer auf eine Command zum freigeben
 * @return 0 bei Erfolg, <0 bei Fehler
 */
int shell_unregister(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif // SHELL_H