#ifndef UART_LIB
#define UART_LIB

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"

void rpi_uart_init(uint8_t core_num, uint8_t priority);
void rpi_uart_close(void);

uint8_t fifo_getTXSize(void);
void sendRPi(uint16_t reg, uint8_t* data, uint16_t size);

#endif