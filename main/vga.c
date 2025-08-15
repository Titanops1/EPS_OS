// vga.c - Implementierung der VGA-Befehle für UART-GPU (z. B. RP2040)
#include "vga.h"
#include "uart_lib.h"
#include "../../register_def.h"
#include <string.h>

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

void vga_load_image_data(const uint8_t* data, uint16_t length) {
	vga_send_command(VGA_CMD_LOAD_IMAGE_DATA, data, length);
}

void vga_scroll(int16_t dx, int16_t dy) {
	uint16_t data[4] = { dx, dy};
	vga_send_command(VGA_CMD_SCROLL, data, 2);
}