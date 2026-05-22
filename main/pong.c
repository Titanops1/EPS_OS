#include "vga.h"
#include "pong.h"
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 480

#define PADDLE_WIDTH   10
#define PADDLE_HEIGHT  60
#define BALL_SIZE      10

#define PADDLE_SPEED   4
#define BALL_SPEED_X   3
#define BALL_SPEED_Y   2

static int16_t paddle1_y = 200;
static int16_t paddle2_y = 200;
static int16_t ball_x = SCREEN_WIDTH / 2;
static int16_t ball_y = SCREEN_HEIGHT / 2;
static int16_t ball_vx = BALL_SPEED_X;
static int16_t ball_vy = BALL_SPEED_Y;

static void draw_paddle(int x, int y) {
	vga_fill_rect(x, y, PADDLE_WIDTH, PADDLE_HEIGHT, 255, 255, 255, 255);
}

static void draw_ball(int x, int y) {
	vga_fill_circle(x, y, BALL_SIZE, 255, 255, 255, 255);
}

static void update_ball(void) {
	ball_x += ball_vx;
	ball_y += ball_vy;

	// Wände oben/unten
	if (ball_y <= 0 || ball_y + BALL_SIZE >= SCREEN_HEIGHT) {
		ball_vy = -ball_vy;
	}

	// Paddle 1
	if (ball_x <= 20 &&
		ball_y + BALL_SIZE >= paddle1_y &&
		ball_y <= paddle1_y + PADDLE_HEIGHT) {
		ball_vx = -ball_vx;
		ball_x = 20;  // Verhindert "Einsinken"
	}

	// Paddle 2
	if (ball_x + BALL_SIZE >= SCREEN_WIDTH - 20 &&
		ball_y + BALL_SIZE >= paddle2_y &&
		ball_y <= paddle2_y + PADDLE_HEIGHT) {
		ball_vx = -ball_vx;
		ball_x = SCREEN_WIDTH - 20 - BALL_SIZE;
	}

	// Ball zurücksetzen, falls außerhalb
	if (ball_x < 0 || ball_x > SCREEN_WIDTH) {
		ball_x = SCREEN_WIDTH / 2;
		ball_y = SCREEN_HEIGHT / 2;
		ball_vx = -ball_vx;
	}
}

static void ai_move(void) {
	if (ball_y < paddle2_y + PADDLE_HEIGHT / 2) paddle2_y -= PADDLE_SPEED;
	if (ball_y > paddle2_y + PADDLE_HEIGHT / 2) paddle2_y += PADDLE_SPEED;

	if (paddle2_y < 0) paddle2_y = 0;
	if (paddle2_y + PADDLE_HEIGHT > SCREEN_HEIGHT)
		paddle2_y = SCREEN_HEIGHT - PADDLE_HEIGHT;
}

void pong_run(void) {
	while (1) {
		vga_clear_screen(0, 0, 0);

		// Eingabe simulieren (automatisch Paddle 1 bewegen mit Ball)
		if (ball_y < paddle1_y + PADDLE_HEIGHT / 2) paddle1_y -= PADDLE_SPEED;
		if (ball_y > paddle1_y + PADDLE_HEIGHT / 2) paddle1_y += PADDLE_SPEED;

		if (paddle1_y < 0) paddle1_y = 0;
		if (paddle1_y + PADDLE_HEIGHT > SCREEN_HEIGHT)
			paddle1_y = SCREEN_HEIGHT - PADDLE_HEIGHT;

		ai_move();
		update_ball();

		draw_paddle(10, paddle1_y);
		draw_paddle(SCREEN_WIDTH - 20, paddle2_y);
		draw_ball(ball_x, ball_y);
		vga_swap_buffers();
		// künstliche Verzögerung (abhängig vom System anpassen!)
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}