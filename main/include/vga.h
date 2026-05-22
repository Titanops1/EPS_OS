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

// ================================
// Widget Types
// ================================
typedef enum {
	WIDGET_LABEL,
	WIDGET_TEXTBOX,
	WIDGET_BUTTON,
	WIDGET_CIRCLE
} widget_type_t;

// ================================
// Structures
// ================================
typedef struct widget_t {
	widget_type_t type;

	uint16_t x, y;
	uint16_t width, height;
	uint16_t raduis;

	uint8_t needs_redraw;

	struct widget_t *next;
} widget_t;

typedef struct label_t {
	widget_t base;
	char text[128];
	uint16_t cursor_pos;
} label_t;

typedef struct text_run {
    const char *text;       // Null-terminated
    uint32_t fg;            // Textfarbe
    uint32_t bg;            // Hintergrundfarbe (optional)
    struct text_run *next;
} text_run_t;

typedef struct {
    widget_t base;
    text_run_t *first_run;
} textbox_t;

typedef struct button_t {
	widget_t base;

	char text[64];

	uint32_t color;
	uint32_t text_color;

	void (*on_click)(struct widget_t *w);

} button_t;

typedef struct circle_t {
	widget_t base;
	uint32_t color;
} circle_t;

typedef struct window_t {
	uint32_t id;

	uint16_t x, y;
	uint16_t width, height;

	uint8_t visible;
	uint8_t dirty;
	uint8_t frameless;

	widget_t *first_widget;

} window_t;

window_t* createForm(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t frameless);
void destroyForm(window_t *win);
void set_active_window(window_t *win);
window_t* get_active_window(void);
// ================================
// Widget API
// ================================
label_t* createLabel(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
textbox_t* createTextbox(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
button_t* createButton(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *text);
widget_t* window_find_widget_at(window_t *win, uint16_t tx, uint16_t ty);
void handle_touch_event(window_t *win, uint16_t x, uint16_t y);
void widget_click(widget_t *w);
void window_add_widget(window_t *win, widget_t *w);
void label_set_text(label_t *lb, const char *text);
void textbox_set_text(textbox_t *tb, const char *text, uint32_t fg);
void textbox_clear(textbox_t *tb);
circle_t* createCircle(uint16_t x, uint16_t y, uint16_t radius);
void circle_set_color(circle_t *circle, uint32_t color);
void circle_set_pos(circle_t *circle, uint16_t x, uint16_t y);
void circle_set_radius(circle_t *circle, uint16_t radius);
// ================================
// Rendering
// ================================
void render_widget(window_t *win, widget_t *w);
void render_window(window_t *win);
void gui_render(void);
void gui_task(void);

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
void vga_draw_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void vga_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void vga_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void vga_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void vga_clear_screen(uint8_t r, uint8_t g, uint8_t b);
void vga_draw_circle(uint16_t x, uint16_t y, uint16_t radius, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void vga_fill_circle(uint16_t x, uint16_t y, uint16_t radius, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void vga_draw_char(uint16_t x, uint16_t y, const char ch, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void vga_draw_text(uint16_t x, uint16_t y, const char* text, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void drawText(const char* text, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void vga_blit_image(uint16_t x, uint16_t y, const uint8_t* img_data, uint16_t w, uint16_t h);
void vga_set_clip_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void vga_reset_clip_rect(void);
void vga_swap_buffers(void);
void vga_draw_triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void vga_fill_triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);

// --- Erweiterungsfunktionen ---
void vga_load_font(const uint8_t* font_data, uint16_t length);
void vga_set_font(uint8_t font_id);
void vga_set_draw_mode(uint16_t mode);
void vga_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void vga_load_image_data(uint16_t x, uint16_t y, uint16_t *img_data, uint16_t length);
void vga_scroll(int16_t dx, int16_t dy);

void run_graphics_test();
void draw_progress_circle(uint16_t cx, uint16_t cy, uint16_t radius, float percent);
void draw_spinner(uint16_t cx, uint16_t cy, uint16_t r, float angle);

void init_vga();