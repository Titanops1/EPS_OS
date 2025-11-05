// vga.h - Grafik-Befehle für UART-GPU (z. B. RP2040)
#include <stdint.h>

// --- Befehlscodes ---
#define VGA_CMD_DRAW_PIXEL       0x01
#define VGA_CMD_DRAW_LINE        0x02
#define VGA_CMD_DRAW_RECT        0x03
#define VGA_CMD_FILL_RECT        0x04
#define VGA_CMD_CLEAR_SCREEN     0x05
#define VGA_CMD_DRAW_CIRCLE      0x06
#define VGA_CMD_FILL_CIRCLE      0x07
#define VGA_CMD_DRAW_TEXT        0x08
#define VGA_CMD_BLIT_IMAGE       0x09
#define VGA_CMD_SET_CLIP_RECT    0x0A
#define VGA_CMD_RESET_CLIP_RECT  0x0B
#define VGA_CMD_SWAP_BUFFERS     0x0C
#define VGA_CMD_DRAW_TRIANGLE    0x0D
#define VGA_CMD_FILL_TRIANGLE    0x0E

// --- Erweiterungen ---
#define VGA_CMD_LOAD_FONT        0x10
#define VGA_CMD_SET_FONT         0x11
#define VGA_CMD_SET_DRAW_MODE    0x12
#define VGA_CMD_SET_PALETTE      0x13
#define VGA_CMD_LOAD_IMAGE_DATA  0x14
#define VGA_CMD_SCROLL           0x15
#define VGA_CMD_GET_WINDOW       0x16

// --- Hilfsfunktionen zum Senden ---
void vga_send_command(uint16_t cmd, const uint16_t* data, uint16_t len);

// --- Zeichenfunktionen ---
void setCursor(uint16_t x, uint16_t y);
void setCursorNextLine();
uint8_t getYFontSize();
uint8_t getXFontSize();
uint16_t getYCursor();
uint16_t getXCursor();

uint16_t vga_getWindowWidth();
uint16_t vga_getWindowHeigth();
void vga_get_frame_size();
void vga_draw_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);
void vga_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t r, uint8_t g, uint8_t b);
void vga_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint8_t g, uint8_t b);
void vga_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint8_t g, uint8_t b);
void vga_clear_screen(uint8_t r, uint8_t g, uint8_t b);
void vga_draw_circle(uint16_t x, uint16_t y, uint16_t radius, uint8_t r, uint8_t g, uint8_t b);
void vga_fill_circle(uint16_t x, uint16_t y, uint16_t radius, uint8_t r, uint8_t g, uint8_t b);
void vga_draw_text(uint16_t x, uint16_t y, const char* text, uint8_t r, uint8_t g, uint8_t b);
void drawText(const char* text, uint8_t r, uint8_t g, uint8_t b);
void vga_blit_image(uint16_t x, uint16_t y, const uint8_t* img_data, uint16_t w, uint16_t h);
void vga_set_clip_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void vga_reset_clip_rect(void);
void vga_swap_buffers(void);
void vga_draw_triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint8_t r, uint8_t g, uint8_t b);
void vga_fill_triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint8_t r, uint8_t g, uint8_t b);

// --- Erweiterungsfunktionen ---
void vga_load_font(const uint8_t* font_data, uint16_t length);
void vga_set_font(uint8_t font_id);
void vga_set_draw_mode(uint8_t mode);
void vga_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void vga_load_image_data(uint16_t x, uint16_t y, uint16_t *img_data, uint16_t length);
void vga_scroll(int16_t dx, int16_t dy);

void run_graphics_test();
void draw_progress_circle(uint16_t cx, uint16_t cy, uint16_t radius, float percent);
void draw_spinner(uint16_t cx, uint16_t cy, uint16_t r, float angle);