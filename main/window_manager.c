#include "window_manager.h"

#define MAX_WINDOWS		10

//#define DEBUG_FOCUS

typedef struct {
	int id;
	int last_id;
	TaskHandle_t task;
	bool active;
	uint8_t priority;
	void (*on_focus_gained)(void);
    void (*on_focus_lost)(void);
} app_t;

static app_t app_table[MAX_WINDOWS];
int next_app_id = 1;

static int current_focus_id = -1;
static SemaphoreHandle_t window_mutex;

void window_manager_init(void) {
	memset(app_table, 0, sizeof(app_table));
	window_mutex = xSemaphoreCreateMutex();
	current_focus_id = -1;
}

bool focus_request(int app_id) {
	xSemaphoreTake(window_mutex, portMAX_DELAY);
	if (current_focus_id == -1 || current_focus_id == app_id) {
		current_focus_id = app_id;
		xSemaphoreGive(window_mutex);
		return true;
	}

	uint8_t current_prio = 0;
	uint8_t requested_prio = 0;
	int current_id = 0;
	int requested_id = 0;
	for (int i = 0; i < MAX_WINDOWS; i++)
	{
		if (app_table[i].id == app_id)
		{
			requested_prio = app_table[i].priority;
			requested_id = i;
		}
		else if (app_table[i].id == current_focus_id)
		{
			current_prio = app_table[i].priority;
			current_id = i;
		}
	}

	if(current_prio >= requested_prio)
	{
		xSemaphoreGive(window_mutex);
		return false;
	}

	app_table[requested_id].last_id = current_focus_id;
	current_focus_id = app_id;
	if(app_table[current_id].on_focus_lost)
	{
		app_table[current_id].on_focus_lost();
	}
	#ifdef DEBUG_FOCUS
	printf("[WM] Requested Focus -> %d (prev %d)\n", current_focus_id, app_table[current_focus_id].id);
	fflush(stdout);
	#endif
	xSemaphoreGive(window_mutex);
	return true;
}

bool focus_has(int app_id) {
	bool result;
	xSemaphoreTake(window_mutex, portMAX_DELAY);
	result = (current_focus_id == app_id);
	xSemaphoreGive(window_mutex);
	return result;
}

void focus_release(int app_id) {
	xSemaphoreTake(window_mutex, portMAX_DELAY);
	int current_id = -1;
	int requested_id = -1;
	for (int i = 0; i < MAX_WINDOWS; i++)
	{
		if (app_table[i].id == current_focus_id)
		{
			current_id = i;
			break;
		}
	}

	for (int i = 0; i < MAX_WINDOWS; i++)
	{
		if (app_table[i].id == app_table[current_id].last_id)
		{
			requested_id = i;
			break;
		}
	}

	current_focus_id = app_table[current_id].last_id;
	app_table[current_id].last_id = -1;
	if(requested_id > -1 && current_focus_id > -1)
	{
		if(app_table[requested_id].on_focus_gained)
		{
			app_table[requested_id].on_focus_gained();
		}
	}
	#ifdef DEBUG_FOCUS
	printf("[WM] Release Focus -> %d (prev %d)\n", current_focus_id, app_table[current_id].id);
	fflush(stdout);
	#endif
	xSemaphoreGive(window_mutex);
}

void window_set_priority(int app_id, uint8_t priority)
{
	xSemaphoreTake(window_mutex, portMAX_DELAY);
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (app_table[i].id == app_id) {
			if(priority > MAX_WINDOW_PRIORITY)
			{
				app_table[i].priority = MAX_WINDOW_PRIORITY;
			}
			else
			{
				app_table[i].priority = priority;
			}
			break;
		}
	}
	xSemaphoreGive(window_mutex);
}

void window_inc_priority(int app_id){
	xSemaphoreTake(window_mutex, portMAX_DELAY);
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (app_table[i].id == app_id) {
			if(app_table[i].priority < MAX_WINDOW_PRIORITY)
			{
				app_table[i].priority++;
			}
			break;
		}
	}
	xSemaphoreGive(window_mutex);
}

void window_dec_priority(int app_id){
	xSemaphoreTake(window_mutex, portMAX_DELAY);
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (app_table[i].id == app_id) {
			if(app_table[i].priority > 0)
			{
				app_table[i].priority--;
			}
			break;
		}
	}
	xSemaphoreGive(window_mutex);
}

int window_register(const char *name, TaskHandle_t task, void (*on_focus_gained)(void), void (*on_focus_lost)(void)) {
	xSemaphoreTake(window_mutex, portMAX_DELAY);
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (!app_table[i].active) {
			app_table[i].id = next_app_id++;
			app_table[i].last_id = -1;
			app_table[i].task = task;
			app_table[i].active = true;
			app_table[i].priority = 1;
			app_table[i].on_focus_gained = on_focus_gained;
			app_table[i].on_focus_lost = on_focus_lost;
			xSemaphoreGive(window_mutex);
			return app_table[i].id;
		}
	}
	xSemaphoreGive(window_mutex);
	return -1; // Kein Platz
}

void window_unregister(int id) {
	xSemaphoreTake(window_mutex, portMAX_DELAY);
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (app_table[i].id == id) {
			app_table[i].active = false;
			app_table[i].priority = 1;
			if (current_focus_id == id) {
				current_focus_id = app_table[i].last_id; // Fokus freigeben
			}
			app_table[i].last_id = -1;
			break;
		}
	}
	xSemaphoreGive(window_mutex);
}