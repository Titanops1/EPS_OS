// vga.c - Implementierung der VGA-Befehle für UART-GPU (z. B. RP2040)
#include "vga.h"
#include "uart_lib.h"
#include "../../register_def.h"
#include <string.h>

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

uint16_t cursor_x = 0;
uint16_t cursor_y = 0;
uint16_t font_Xsize = 6;
uint16_t font_Ysize = 8;

uint16_t frame_width = 0;
uint16_t frame_height = 0;

#define MAX_DATA_SIZE	128
#define SPINNER_POINTS	12

// ================================
// Config
// ================================
#define MAX_WINDOWS 16

// ================================
// Globals
// ================================
static window_t *window_list[MAX_WINDOWS];
static uint8_t window_count = 0;
static window_t *active_window = NULL;
static uint8_t gui_need_redraw = 0;
static uint32_t next_window_id = 1;

// ================================
// Internal helpers
// ================================
static window_t* find_window_by_id(uint32_t id)
{
	for (uint8_t i = 0; i < window_count; i++) {
		if (window_list[i] && window_list[i]->id == id)
			return window_list[i];
	}
	return NULL;
}

static void mark_gui_dirty(void)
{
	gui_need_redraw = 1;
}

void label_trim_to_fit(label_t *lb, int max_px_width, int char_width) {
	int max_chars = max_px_width / char_width;
	int len = strlen(lb->text);
	if (len > max_chars) {
		lb->text[max_chars] = '\0';
	}
}

void textbox_wrap(const char *src, char lines[][64], int max_lines, int max_chars_per_line)
{
	int line = 0;
	int pos = 0;

	while (*src && line < max_lines) {
		int count = 0;

		// kopiere Zeichen bis zur max. Linienlänge oder bis \n
		while (*src && *src != '\n' && count < max_chars_per_line) {
			lines[line][count++] = *src++;
		}

		lines[line][count] = '\0';
		line++;

		// \n explizit überspringen
		if (*src == '\n') src++;

		// Falls aktuelles Wort weitergeht → skippen bis Wortende
		// Damit keine halben Wörter stehen bleiben
		while (*src && *src != ' ' && *src != '\n' && count == max_chars_per_line) {
			src++;
		}

		// Leerzeichen am Zeilenanfang vermeiden
		while (*src == ' ') src++;
	}
}

// ================================
// Window API
// ================================

window_t* createForm(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t frameless)
{
	if (window_count >= MAX_WINDOWS)
		return NULL;

	window_t *win = (window_t*)psram_malloc(sizeof(window_t));
	if (!win)
		return NULL;

	memset(win, 0, sizeof(window_t));

	win->id = next_window_id++;
	win->x = x;
	win->y = y;
	win->width = w;
	win->height = h;
	win->frameless = frameless;
	win->visible = 1;
	win->dirty = 1;

	window_list[window_count] = win;
	window_count++;

	//printf("Neues Fenster %d\n", window_count-1);

	mark_gui_dirty();
	set_active_window(win);
	return win;
}

void destroyForm(window_t *win)
{
	if (!win) return;

	// Remove from list
	for (uint8_t i = 0; i < window_count; i++) {
		if (window_list[i] == win) {
			for (uint8_t j = i; j < window_count - 1; j++) {
				window_list[j] = window_list[j + 1];
			}
			window_list[window_count - 1] = NULL;
			window_count--;
			break;
		}
	}

	// Free widgets
	widget_t *w = win->first_widget;
	while (w) {
		widget_t *next = w->next;
		psram_free(w);
		w = next;
	}

	if (active_window == win)
	{
		if(window_count > 0)
		{
			set_active_window(window_list[window_count-1]);
		}
		else
		{
			set_active_window(active_window = window_list[0]);
		}
		//printf("Neues Fenster %d\n", window_count-1);
	}

	psram_free(win);
	mark_gui_dirty();
}

void set_active_window(window_t *win)
{
	if (!win) return;
	active_window = win;
	win->dirty = 1;
	mark_gui_dirty();
}

window_t* get_active_window(void)
{
	return active_window;
}

// ================================
// Widget API
// ================================

label_t* createLabel(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	label_t *lb = (label_t*)psram_malloc(sizeof(label_t));
	if (!lb) return NULL;

	memset(lb, 0, sizeof(label_t));

	lb->base.type = WIDGET_LABEL;
	lb->base.x = x;
	lb->base.y = y;
	lb->base.width = w;
	lb->base.height = h;

	return lb;
}

textbox_t* createTextbox(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	textbox_t *tb = (textbox_t*)psram_malloc(sizeof(textbox_t));
	if (!tb) return NULL;

	memset(tb, 0, sizeof(textbox_t));

	tb->base.type = WIDGET_TEXTBOX;
	tb->base.x = x;
	tb->base.y = y;
	tb->base.width = w;
	tb->base.height = h;

	return tb;
}

button_t* createButton(
	uint16_t x,
	uint16_t y,
	uint16_t width,
	uint16_t height,
	const char *text
)
{
	button_t *btn =
		(button_t*)malloc(sizeof(button_t));

	if (!btn)
		return NULL;

	//--------------------------------
	// Base setzen
	//--------------------------------

	btn->base.type = WIDGET_BUTTON;

	btn->base.x = x;
	btn->base.y = y;

	btn->base.width = width;
	btn->base.height = height;

	btn->base.raduis = 0;

	btn->base.needs_redraw = 1;
	btn->base.next = NULL;

	//--------------------------------
	// Text setzen
	//--------------------------------

	strncpy(
		btn->text,
		text,
		sizeof(btn->text) - 1
	);

	btn->text[
		sizeof(btn->text) - 1
	] = 0;

	//--------------------------------
	// Farben
	//--------------------------------
	btn->color = 0x404040;
	btn->text_color = 0xFFFFFF;
	btn->on_click = NULL;

	return btn;
}

void widget_click(widget_t *w)
{
	if (!w)
		return;

	switch (w->type) {
		case WIDGET_BUTTON: {
			button_t *btn = (button_t*)w;

			if (btn->on_click)
				btn->on_click(w);

		} break;

		default:
			break;
	}
}

widget_t* window_find_widget_at(window_t *win, uint16_t tx, uint16_t ty)
{
	if (!win)
		return NULL;

	widget_t *w = win->first_widget;
	uint16_t offset_y = (win->frameless == 0) ? 10 : 0;

	while (w) {
		uint16_t ax = win->x + w->x;
		uint16_t ay = win->y + offset_y + w->y;
		//--------------------------------
		// Rechteck-Test
		//--------------------------------
		if (tx >= ax &&
			tx < ax + w->width &&
			ty >= ay &&
			ty < ay + w->height)
		{
			return w;
		}
		w = w->next;
	}
	return NULL;
}

void handle_touch_event(window_t *win, uint16_t x, uint16_t y)
{
	widget_t *w =
		window_find_widget_at(
			win,
			x,
			y
		);

	if (w) {
		//printf("Widget getroffen: %d\n", w->type);
		widget_click(w);
	}
}

void window_add_widget(window_t *win, widget_t *w)
{
	if (!win || !w) return;

	w->next = win->first_widget;
	win->first_widget = w;
	win->dirty = 1;
	mark_gui_dirty();
}

void label_set_text(label_t *lb, const char *text)
{
	if (!lb || !text) return;

	strncpy(lb->text, text, sizeof(lb->text) - 1);
	lb->text[sizeof(lb->text) - 1] = 0;
	lb->base.needs_redraw = 1;
	mark_gui_dirty();
}

void textbox_set_text(textbox_t *tb, const char *text, uint32_t fg)
{
	text_run_t *run = malloc(sizeof(text_run_t));
	run->text = strdup(text);
	run->fg = fg;
	run->next = NULL;

	if (!tb->first_run) {
		tb->first_run = run;
	} else {
		text_run_t *cur = tb->first_run;
		while (cur->next) cur = cur->next;
		cur->next = run;
	}

	tb->base.needs_redraw = 1; // neu rendern
	mark_gui_dirty();
}

void textbox_clear(textbox_t *tb)
{
	text_run_t *run = tb->first_run;
	while (run) {
		text_run_t *next = run->next;
		free(run->text);
		free(run);
		run = next;
	}
	tb->first_run = NULL;
	tb->base.needs_redraw = 1; // neu rendern
	mark_gui_dirty();
}

circle_t* createCircle(uint16_t x, uint16_t y, uint16_t radius)
{
	circle_t *circle = (circle_t*)psram_malloc(sizeof(circle_t));
	if (!circle) return NULL;

	memset(circle, 0, sizeof(circle_t));

	circle->base.type = WIDGET_CIRCLE;
	circle->base.x = x;
	circle->base.y = y;
	circle->base.raduis = radius;
	circle->color = 0xFFFFFF; //White

	return circle;
}

void circle_set_color(circle_t *circle, uint32_t color)
{
	circle->color = color;
	circle->base.needs_redraw = 1;
	mark_gui_dirty();
}

void circle_set_pos(circle_t *circle, uint16_t x, uint16_t y)
{
	circle->base.x = x;
	circle->base.y = y;
	circle->base.needs_redraw = 1;
	mark_gui_dirty();
}

void circle_set_radius(circle_t *circle, uint16_t radius)
{
	circle->base.raduis = radius;
	circle->base.needs_redraw = 1;
	mark_gui_dirty();
}

// ================================
// Rendering
// ================================
void render_widget(window_t *win, widget_t *w)
{
	uint16_t client_offset_y = (win->frameless == 0) ? 10 : 0;

	uint16_t ax = win->x + w->x;
	uint16_t ay = win->y + w->y + client_offset_y;

	if (!w->needs_redraw && !win->dirty)
		return;

	switch (w->type) {
		case WIDGET_LABEL: {
			label_t *lb = (label_t*)w;
			label_trim_to_fit(lb,  w->width, getXFontSize());
			//vga_draw_rect(ax, ay, w->width, w->height, 0, 255, 0, 255);
			vga_draw_text(ax + 2, ay + 2, lb->text, 0, 0, 0, 255);
		} break;

		case WIDGET_TEXTBOX: {
			textbox_t *tb = (textbox_t*)w;
			uint16_t ty = ay + 2;

			for (text_run_t *run = tb->first_run; run; run = run->next) {
				uint16_t cx = ax + 2;

				// printf(
				// 	"run=%p next=%p text=%p\n",
				// 	run,
				// 	run->next,
				// 	run->text
				// );

				// Versuche Text vorsichtig zu lesen
				//printf("text preview: %.32s\n", run->text);

				const char *p = run->text;
				while (*p) {
					// Umbrüche unterstützen
					if (*p == '\n') {
						ty += getYFontSize();
						cx = ax + 2;
						p++;
						continue;
					}

					// Zeichenbreite holen
					uint16_t char_width = getXFontSize();

					// zeichne Zeichen
					vga_draw_char(cx, ty, *p, (run->fg>>16), (run->fg>>8), (run->fg&0xFF), 255);

					cx += char_width;
					p++;
				}
				ty += getYFontSize();
			}
		} break;

		case WIDGET_BUTTON: {
			button_t *btn = (button_t*)w;
			//--------------------------------
			// Hintergrund
			//--------------------------------
			vga_fill_rect(
				ax,
				ay,
				w->width,
				w->height,
				(btn->color >> 16),
				(btn->color >> 8),
				(btn->color & 0xFF),
				255
			);
			//--------------------------------
			// Text zentrieren
			//--------------------------------
			uint16_t text_width =
				strlen(btn->text) *
				getXFontSize();

			uint16_t tx =
				ax +
				(w->width - text_width) / 2;

			uint16_t ty =
				ay +
				(w->height - getYFontSize()) / 2;

			vga_draw_text(
				tx,
				ty,
				btn->text,
				(btn->text_color >> 16),
				(btn->text_color >> 8),
				(btn->text_color & 0xFF),
				255
			);

		} break;
		case WIDGET_CIRCLE: {
			circle_t *cr = (circle_t*)w;
			vga_fill_circle(cr->base.x, cr->base.y, cr->base.raduis, (cr->color>>16), (cr->color>>8), (cr->color&0xFF), 255);
		} break;
		default:
			vga_draw_rect(ax, ay, w->width, w->height, 255, 255, 255, 255);
			break;
	}

	w->needs_redraw = 0;
}

void render_window(window_t *win)
{
	if (!win->visible)
		return;

	vga_set_clip_rect(win->x, win->y, win->width-1, win->height-1);
	// Window border
	vga_fill_rect(win->x, win->y, win->width, win->height, 255, 255, 255, 255);
	
	widget_t *w = win->first_widget;
	while (w) {
		render_widget(win, w);
		w = w->next;
	}

	if(!win->frameless)
	{
		vga_fill_rect(win->x, win->y, win->width, 10, 128, 128, 128, 255);
	}

	vga_reset_clip_rect();
	win->dirty = 0;
}

void gui_render(void)
{
	if (!gui_need_redraw)
		return;

	//vga_clear_screen(0, 0, 0);

	for (uint8_t i = 0; i < window_count; i++) {
		if (window_list[i]) {
			render_window(window_list[i]);
		}
	}
	vga_swap_buffers();

	gui_need_redraw = 0;
}

// ================================
// Task Loop (call from FreeRTOS)
// ================================

void gui_task(void)
{
	touch_event_t ev;
	uint8_t last_pressed = 0;

	while (1) {
		if (xQueueReceive(touch_queue, &ev, pdMS_TO_TICKS(1000)))
		{
			// printf(
			// 	"Touch X:%d Y:%d P:%d\n",
			// 	ev.x,
			// 	ev.y,
			// 	ev.pressed
			// );
			//--------------------------------
			// Klick nur beim Loslassen
			//--------------------------------
			if (!ev.pressed && last_pressed)
			{
				//handle_touch_event(window_list[window_count-1], ev.x, ev.y);
				handle_touch_event(active_window, ev.x, ev.y);
			}
			last_pressed =ev.pressed;
		}
		gui_render();
		vTaskDelay(16);
	}
}


// Low Level API

void vga_send_command(uint16_t cmd, const uint16_t* data, uint16_t len) {
	if (len > 251)
	{
		return; // Sicherheit: max. 251 Payload-Bytes erlaubt
	}

	uint16_t buf[255];
	buf[0] = cmd;           // Befehl
	buf[1] = len;    		// Länge High

	for (uint16_t i = 0; i < len; i++)
	{
		buf[2 + i] = data[i]; // Payload kopieren
	}

	sendRPi(REG_VGA, buf, len + 2); // Sende gesamte Nachricht
}

// Hilfsfunktion zum Hinzufügen von Farbwerten
static inline void append_rgb(uint8_t* buf, uint8_t r, uint8_t g, uint8_t b) {
	buf[0] = r;
	buf[1] = g;
	buf[2] = b;
}

void setCursor(uint16_t x, uint16_t y)
{
	cursor_x = x;
	cursor_y = y;
}

void setCursorNextLine()
{
	if(cursor_y + font_Ysize > frame_height-font_Ysize)
	{
		cursor_x = 2;
		vga_scroll(0, font_Ysize);
	}
	else
	{
		cursor_x = 2;
		cursor_y += font_Ysize;
	}
}

uint8_t getYFontSize()
{
	return font_Ysize;
}

uint8_t getXFontSize()
{
	return font_Xsize;
}

uint16_t getYCursor()
{
	return cursor_y;
}
uint16_t getXCursor()
{
	return cursor_x;
}

uint16_t vga_getWindowWidth()
{
	return frame_width;
}

uint16_t vga_getWindowHeigth()
{
	return frame_height;
}

void vga_get_frame_size()
{
	uint16_t buf[2] = {1, 1};
	uint16_t timeout_cnt = 0;
	vga_event_t ev;
	vga_send_command(VGA_CMD_GET_WINDOW, &buf, 2);
	if (xQueueReceive(vga_queue, &ev, pdMS_TO_TICKS(1000)))
	{
		if(ev.cmd == VGA_CMD_GET_WINDOW)
		{
			frame_width = ev.data0;
			frame_height = ev.data1;
		}
	}
	else
	{
		printf("No Data received\n");
	}
}

void vga_draw_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t data[6] = { x, y, r, g, b, alpha};
	vga_send_command(VGA_CMD_DRAW_PIXEL, data, 6);
}

void vga_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t data[8] = {x0, y0, x1, y1, r, g, b, alpha};
	vga_send_command(VGA_CMD_DRAW_LINE, data, 8);
}

void vga_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t data[8] = {
		x, y,
		w, h,
		r, g, b, alpha
	};
	vga_send_command(VGA_CMD_DRAW_RECT, data, 8);
}

void vga_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t data[8] = {
		x, y,
		w, h,
		r, g, b, alpha
	};
	vga_send_command(VGA_CMD_FILL_RECT, data, 8);
}

void vga_clear_screen(uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[3] = { r, g, b };
	vga_send_command(VGA_CMD_CLEAR_SCREEN, data, 3);
}

void vga_draw_circle(uint16_t x, uint16_t y, uint16_t radius, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t data[7] = {
		x, y,
		radius,
		r, g, b, alpha
	};
	vga_send_command(VGA_CMD_DRAW_CIRCLE, data, 7);
}

void vga_fill_circle(uint16_t x, uint16_t y, uint16_t radius, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t data[7] = {
		x, y,
		radius,
		r, g, b, alpha
	};
	vga_send_command(VGA_CMD_FILL_CIRCLE, data, 7);
}

void vga_draw_char(uint16_t x, uint16_t y, const char ch, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t buf[7];
	buf[0] = x;
	buf[1] = y;
	buf[2] = r; buf[3] = g; buf[4] = b;
	buf[5] = alpha;
	buf[6] = (uint16_t)ch;  // korrekt casten
	
	vga_send_command(VGA_CMD_DRAW_TEXT, buf, 7);
}

void vga_draw_text(uint16_t x, uint16_t y, const char* text, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t len = strlen(text);
	uint16_t buf[6 + len];
	buf[0] = x;
	buf[1] = y;
	buf[2] = r; buf[3] = g; buf[4] = b;
	buf[5] = alpha;
	for (uint16_t i = 0; i < len; i++) {
		buf[6 + i] = (uint16_t)text[i];  // korrekt casten
	}
	
	vga_send_command(VGA_CMD_DRAW_TEXT, buf, 6 + len);
}

void drawText(const char* text, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha)
{
	uint16_t len = strlen(text);
	uint16_t buf[6 + len];
	buf[0] = cursor_x;
	buf[1] = cursor_y;
	buf[2] = r; buf[3] = g; buf[4] = b;
	buf[5] = alpha;
	for (uint16_t i = 0; i < len; i++) {
		buf[6 + i] = (uint16_t)text[i];  // korrekt casten
	}
	
	vga_send_command(VGA_CMD_DRAW_TEXT, buf, 6 + len);
	cursor_x += (len*font_Xsize);
	//printf("Cursor: %d,Pixel im String: %d, Zeichen: %d\nString %s\n", cursor_x, len*font_Xsize, len, text);
}

void vga_blit_image(uint16_t x, uint16_t y, const uint8_t* img_data, uint16_t w, uint16_t h) {
	// Hier nur Metadaten senden, Bilddaten sollten vorher per LOAD_IMAGE_DATA gesendet werden
	uint16_t data[4] = {
		x, y,
		w, h
	};
	vga_send_command(VGA_CMD_BLIT_IMAGE, data, 4);
}

void vga_set_clip_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
	uint16_t data[4] = {
		x, y,
		w, h
	};
	vga_send_command(VGA_CMD_SET_CLIP_RECT, data, 4);
}

void vga_reset_clip_rect(void) {
	vga_send_command(VGA_CMD_RESET_CLIP_RECT, NULL, 0);
}

void vga_swap_buffers(void) {
	vga_send_command(VGA_CMD_SWAP_BUFFERS, NULL, 0);
	vTaskDelay(pdMS_TO_TICKS(10));
}

void vga_draw_triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t data[10] = {
		x1, y1,
		x2, y2,
		x3, y3,
		r, g, b, alpha
	};
	vga_send_command(VGA_CMD_DRAW_TRIANGLE, data, 10);
}

void vga_fill_triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t data[10] = {
		x1, y1,
		x2, y2,
		x3, y3,
		r, g, b, alpha
	};
	vga_send_command(VGA_CMD_FILL_TRIANGLE, data, 10);
}

void vga_load_font(const uint8_t* font_data, uint16_t length) {
	vga_send_command(VGA_CMD_LOAD_FONT, font_data, length);
}

void vga_set_font(uint8_t font_id) {
	vga_send_command(VGA_CMD_SET_FONT, &font_id, 1);
}

void vga_set_draw_mode(uint16_t mode) {
	vga_send_command(VGA_CMD_SET_DRAW_MODE, &mode, 1);
}

void vga_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	uint16_t data[5] = { index, r, g, b, alpha};
	vga_send_command(VGA_CMD_SET_PALETTE, data, 5);
}

void vga_load_image_data(uint16_t x, uint16_t y, uint16_t *img_data, uint16_t length) {
	if(length > MAX_DATA_SIZE)
	{
		return;
	}
	uint16_t data[2+length];
	data[0] = x;
	data[1] = y;

	for(uint16_t i = 0; i < length; i++)
	{
		data[2+i] = img_data[i];
	}
	vga_send_command(VGA_CMD_LOAD_IMAGE_DATA, data, length+2);
}

void vga_scroll(int16_t dx, int16_t dy) {
	uint16_t data[4] = { dx, dy};
	vga_send_command(VGA_CMD_SCROLL, data, 2);
}

void run_graphics_test() {
	vga_clear_screen(0, 0, 0);
	vga_swap_buffers();

	// Farbverlauf von oben nach unten (rot)
	for (int y = 0; y < 480; y += 10) {
		vga_draw_rect(0, y, 640, 10, 255, 0, 0, 255);  // Rotverlauf
		vga_swap_buffers();
	}

	// Diagonale Linien von links oben nach rechts unten
	for (int i = 0; i < 640; i += 40) {
		vga_draw_line(0, 0, i, 479, 0, 255, 0, 255);  // Grün
		vga_swap_buffers();
	}

	for (int i = 0; i < 480; i += 40) {
		vga_draw_line(0, 0, 639, i, 0, 255, 0, 255);  // Weitere grüne Linien
		vga_swap_buffers();
	}

	// Rechtecke in verschiedenen Farben
	vga_draw_rect(100, 100, 100, 50, 255, 0, 0, 255);   // Rot
	vga_swap_buffers();
	vga_draw_rect(250, 100, 100, 50, 0, 255, 0, 255);   // Grün
	vga_swap_buffers();
	vga_draw_rect(400, 100, 100, 50, 0, 0, 255, 255);   // Blau
	vga_swap_buffers();

	// Textanzeige
	vga_draw_text(20, 300, "VGA Testbild", 255, 255, 255, 255);
	vga_swap_buffers();
	vga_draw_text(20, 320, "RP2040 GPU Test", 255, 255, 0, 255);
	vga_swap_buffers();
}

void draw_progress_circle(uint16_t cx, uint16_t cy, uint16_t radius, float percent)
{
	float end_angle = (percent / 100.0f) * 360.0f;
	for (float a = 0; a < end_angle; a += 2.0f)
	{
		float rad = a * 3.1415926f / 180.0f;
		int16_t x = cx + cosf(rad) * radius;
		int16_t y = cy + sinf(rad) * radius;
		vga_draw_pixel(x, y, 0, 255, 0, 255);
	}
}

void draw_spinner(uint16_t cx, uint16_t cy, uint16_t r, float angle)
{
	for (int i = 0; i < SPINNER_POINTS; i++) {
		float a = angle + (360.0f / SPINNER_POINTS) * i;
		float rad = a * 3.1415926f / 180.0f;
		int16_t x = cx + cosf(rad) * r;
		int16_t y = cy + sinf(rad) * r;

		// Verlauf von hell nach dunkel (optional leicht bläulich)
		uint8_t alpha;
		if (i == 3)
			alpha = 255; // 255
		else if (i == 2)
			alpha = 180;//180;
		else if (i == 1)
			alpha = 120;//120;
		else if (i == 0)
			alpha = 70;//70;
		else
			alpha = 30;//30;  // sehr dunkel

		// leicht bläulicher Farbton für schönes "Loading"-Aussehen
		uint8_t r_col = 85;
		uint8_t g_col = 85;
		uint8_t b_col = 255;

		vga_fill_circle(x, y, 3, r_col, g_col, b_col, alpha);
	}
}

void init_vga()
{
	rpi_uart_init(0, configMAX_PRIORITIES-1);
	vga_get_frame_size();
	xTaskCreatePinnedToCore(gui_task, "gui", 1024*4, NULL, 5, NULL, 0);
}