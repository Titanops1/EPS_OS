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

uint16_t cursor_x = 0;
uint16_t cursor_y = 0;
uint16_t font_Xsize = 6;
uint16_t font_Ysize = 8;

uint16_t frame_width = 0;
uint16_t frame_height = 0;

#define MAX_DATA_SIZE	128
#define SPINNER_POINTS	12

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
	cursor_x = 2;
	cursor_y += font_Ysize;
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
	vga_send_command(VGA_CMD_GET_WINDOW, &buf, 2);
	while(getRxComplete() != 1 && timeout_cnt < 1000)
	{
		timeout_cnt++;
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	if(getRxComplete() == 1)
	{
		if(getRxReg() == REG_VGA && getRxData(0) == VGA_CMD_GET_WINDOW)
		{
			frame_width = getRxData(1);
			frame_height = getRxData(2);
			clearRxComplete();
		}
	}
	else
	{
		printf("No Data received\n");
	}
}

void vga_draw_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[5] = { x, y, r, g, b };
	vga_send_command(VGA_CMD_DRAW_PIXEL, data, 5);
}

void vga_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[7] = {x0, y0, x1, y1, r, g, b};
	vga_send_command(VGA_CMD_DRAW_LINE, data, 7);
}

void vga_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[7] = {
		x, y,
		w, h,
		r, g, b
	};
	vga_send_command(VGA_CMD_DRAW_RECT, data, 7);
}

void vga_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[7] = {
		x, y,
		w, h,
		r, g, b
	};
	vga_send_command(VGA_CMD_FILL_RECT, data, 7);
}

void vga_clear_screen(uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[3] = { r, g, b };
	vga_send_command(VGA_CMD_CLEAR_SCREEN, data, 3);
}

void vga_draw_circle(uint16_t x, uint16_t y, uint16_t radius, uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[6] = {
		x, y,
		radius,
		r, g, b
	};
	vga_send_command(VGA_CMD_DRAW_CIRCLE, data, 6);
}

void vga_fill_circle(uint16_t x, uint16_t y, uint16_t radius, uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[6] = {
		x, y,
		radius,
		r, g, b
	};
	vga_send_command(VGA_CMD_FILL_CIRCLE, data, 6);
}

void vga_draw_text(uint16_t x, uint16_t y, const char* text, uint8_t r, uint8_t g, uint8_t b) {
	uint16_t len = strlen(text);
	uint16_t buf[5 + len];
	buf[0] = x;
	buf[1] = y;
	buf[2] = r; buf[3] = g; buf[4] = b;
	
	for (uint16_t i = 0; i < len; i++) {
		buf[5 + i] = (uint16_t)text[i];  // korrekt casten
	}
	
	vga_send_command(VGA_CMD_DRAW_TEXT, buf, 5 + len);
}

void drawText(const char* text, uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t len = strlen(text);
	uint16_t buf[5 + len];
	buf[0] = cursor_x;
	buf[1] = cursor_y;
	buf[2] = r; buf[3] = g; buf[4] = b;
	
	for (uint16_t i = 0; i < len; i++) {
		buf[5 + i] = (uint16_t)text[i];  // korrekt casten
	}
	
	vga_send_command(VGA_CMD_DRAW_TEXT, buf, 5 + len);
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
}

void vga_draw_triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[9] = {
		x1, y1,
		x2, y2,
		x3, y3,
		r, g, b
	};
	vga_send_command(VGA_CMD_DRAW_TRIANGLE, data, 9);
}

void vga_fill_triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[9] = {
		x1, y1,
		x2, y2,
		x3, y3,
		r, g, b
	};
	vga_send_command(VGA_CMD_FILL_TRIANGLE, data, 9);
}

void vga_load_font(const uint8_t* font_data, uint16_t length) {
	vga_send_command(VGA_CMD_LOAD_FONT, font_data, length);
}

void vga_set_font(uint8_t font_id) {
	vga_send_command(VGA_CMD_SET_FONT, &font_id, 1);
}

void vga_set_draw_mode(uint8_t mode) {
	vga_send_command(VGA_CMD_SET_DRAW_MODE, &mode, 1);
}

void vga_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
	uint16_t data[4] = { index, r, g, b };
	vga_send_command(VGA_CMD_SET_PALETTE, data, 4);
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
		vga_draw_rect(0, y, 640, 10, 255, 0, 0);  // Rotverlauf
		vga_swap_buffers();
	}

	// Diagonale Linien von links oben nach rechts unten
	for (int i = 0; i < 640; i += 40) {
		vga_draw_line(0, 0, i, 479, 0, 255, 0);  // Grün
		vga_swap_buffers();
	}

	for (int i = 0; i < 480; i += 40) {
		vga_draw_line(0, 0, 639, i, 0, 255, 0);  // Weitere grüne Linien
		vga_swap_buffers();
	}

	// Rechtecke in verschiedenen Farben
	vga_draw_rect(100, 100, 100, 50, 255, 0, 0);   // Rot
	vga_swap_buffers();
	vga_draw_rect(250, 100, 100, 50, 0, 255, 0);   // Grün
	vga_swap_buffers();
	vga_draw_rect(400, 100, 100, 50, 0, 0, 255);   // Blau
	vga_swap_buffers();

	// Textanzeige
	vga_draw_text(20, 300, "VGA Testbild", 255, 255, 255);
	vga_swap_buffers();
	vga_draw_text(20, 320, "RP2040 GPU Test", 255, 255, 0);
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
		vga_draw_pixel(x, y, 0, 255, 0);
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
		uint8_t brightness;
		if (i == 3)
			brightness = 255; // weiß
		else if (i == 2)
			brightness = 180;
		else if (i == 1)
			brightness = 120;
		else if (i == 0)
			brightness = 70;
		else
			brightness = 30;  // sehr dunkel

		// leicht bläulicher Farbton für schönes "Loading"-Aussehen
		uint8_t r_col = brightness / 3;
		uint8_t g_col = brightness / 3;
		uint8_t b_col = brightness;

		vga_fill_circle(x, y, 3, r_col, g_col, b_col);
	}
}