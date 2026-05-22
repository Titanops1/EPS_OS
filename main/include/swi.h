#ifndef _SWI_H_
#define _SWI_H_

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SWI_MAX_APPS       200
#define SWI_MSG_SIZE       16   // bytes per message (anpassbar)
#define SWI_QUEUE_LEN      16   // messages pro App (anpassbar)

typedef struct {
	uint8_t type;
	uint8_t data[SWI_MSG_SIZE - 1]; // type + payload
} swi_msg_t;

typedef int swi_app_id_t;

/**
 * Initialisiert das SWI/Subsystem.
 * Muss einmalig beim Systemstart aufgerufen werden.
 */
void swi_init(void);

/**
 * Registriert eine App im SWI-System.
 * - task: TaskHandle der App (wird für TaskNotify genutzt)
 * - queue_len: optionale Queue-Länge (0 = default SWI_QUEUE_LEN)
 *
 * Rückgabe: app_id >= 0 bei Erfolg, -1 bei Fehler (kein Platz)
 */
swi_app_id_t swi_register_app(TaskHandle_t task, uint16_t queue_len);

/**
 * Findet eine App im SWI-System.
 * - task: TaskHandle der App (wird für TaskNotify genutzt)
 *
 * Rückgabe: app_id >= 0 bei Erfolg, -1 bei Fehler (kein Platz)
 */
swi_app_id_t swi_get_appId();

/**
 * Fragt ab ob es eine neue Nachricht im SWI-System gibt.
 * - notified: gibt den Notified Wert des sendenden Tasks an
 * - wait_time: wie lange gewartet werden soll bis abgebrochen wird (blockend)
 *
 * Rückgabe: 1 bei Erfolg, 0 bei keiner neuen Nachricht
 */
bool swi_get_notification(uint32_t *notified, uint32_t wait_time);

/**
 * Unregistriert eine App.
 * Gibt alle Ressourcen frei.
 */
void swi_unregister_app(swi_app_id_t app_id);

/**
 * Sendet eine Nachricht an die App (nicht-ISR-Kontext).
 * Gibt true zurück, wenn erfolgreich in die Queue geschrieben wurde.
 */
bool swi_send_message(swi_app_id_t app_id, const swi_msg_t *msg, TickType_t wait_ticks);

/**
 * Sendet eine Nachricht an die App aus einem ISR-Kontext.
 * Gibt true zurück, wenn erfolgreich.
 * Wenn successful und app hat höhere Priorität, kann ein Context-Switch erzwungen werden.
 */
bool swi_send_message_from_isr(swi_app_id_t app_id, const swi_msg_t *msg, BaseType_t *pxHigherPriorityTaskWoken);

/**
 * API die App-seitig non-blocking aufrufen kann, um anstehende Messages abzuholen.
 * - out_msg: Zeiger auf Puffer (muss groß genug sein)
 * - timeout_ticks: Wartezeit in Tick (0 = non-blocking)
 * Rückgabe: true wenn msg gelesen wurde, false sonst.
 */
bool swi_recv_message(swi_app_id_t app_id, swi_msg_t *out_msg, TickType_t timeout_ticks);

/**
 * Leitet eine TaskNotify an die app-Task (non-blocking).
 * Normalerweise wird dies intern aufgerufen; du brauchst es nicht oft.
 */
void swi_notify_app_from_isr(swi_app_id_t app_id, BaseType_t *pxHigherPriorityTaskWoken);

#endif // _SWI_H_