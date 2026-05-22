#ifndef UART_LIB
#define UART_LIB

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"

typedef struct {
	uint16_t cmd;
	uint16_t data0;
	uint8_t data1;
} vga_event_t;
extern QueueHandle_t vga_queue;

typedef struct {
	uint16_t x;
	uint16_t y;
	uint8_t pressed;
} touch_event_t;
extern QueueHandle_t touch_queue;

typedef struct {
	uint16_t cmd;
	uint16_t data0;
} gpio_event_t;
extern QueueHandle_t gpio_queue;

void rpi_uart_init(uint8_t core_num, uint8_t priority);
void rpi_uart_close(void);

uint8_t fifo_getTXSize(void);
void sendRPi(uint16_t reg, uint16_t* data, uint16_t size);

uint8_t getRxComplete(void);
void clearRxComplete(void);
uint16_t getRxReg(void);
uint16_t getRxLen(void);
uint16_t getRxData(uint8_t index);
#endif