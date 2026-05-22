/* UART asynchronous example, that uses separate RX and TX tasks

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "uart_lib.h"
#include "pin_def.h"
#include "../../register_def.h"
#include "systemCalls.h"

static const char *TAG = "uart";

#define RPI_UART	UART_NUM_1
#define MAX_BUFFER_SIZE 512


static SemaphoreHandle_t tx_done_sem = NULL;
static SemaphoreHandle_t send_mutex;
uint8_t tx_disable = 0;

TaskHandle_t UartRxHandle;
TaskHandle_t UartTxHandle;
TaskHandle_t LedRxTxHandle;

QueueHandle_t vga_queue;
QueueHandle_t touch_queue;
QueueHandle_t gpio_queue;

uint8_t led_count = 10;

/***************************************************
 * FIFO Buffer for UART
*/
typedef struct {
	uint8_t data[MAX_BUFFER_SIZE];
	int head;
	int tail;
	int size;
} FifoBuffer;

FifoBuffer txfifo;

uint8_t header[8] = {0xff,0xff,0xff,0xfe,0xff,0xfd,0xff,0xfc};

struct uart_rx_data_t
{
	uint16_t header[4];
	uint16_t reg;
	uint16_t len;
	uint16_t data[MAX_BUFFER_SIZE];

} uart_rx;

struct uart_rx_ctrl_t
{
	uint8_t ctrl_cnt;
	uint8_t complete;
} uart_rx_ctrl;

// Funktion zum Initialisieren des FIFO-Puffers
void fifo_init(FifoBuffer *buffer) {
	buffer->head = 0;
	buffer->tail = 0;
	buffer->size = 0;
}

// Funktion zum Hinzufügen eines Elements zum FIFO-Puffer
int fifo_push(FifoBuffer *buffer, uint8_t value) {
	//ESP_LOGI("UART_FIFO", "Write: '%c'", value);
	if (buffer->size >= MAX_BUFFER_SIZE) {
		// Der Puffer ist voll
		return -1; // Fehlercode für Pufferüberlauf
	}

	buffer->data[buffer->tail] = value;
	buffer->tail = (buffer->tail + 1) % MAX_BUFFER_SIZE;
	buffer->size++;

	return 0; // Erfolgreich
}

// Funktion zum Entfernen und Zurückgeben des ersten Elements aus dem FIFO-Puffer
uint8_t fifo_pop(FifoBuffer *buffer) {
	if (buffer->size <= 0) {
		// Der Puffer ist leer
		return -1; // Fehlercode für Pufferleerstand
	}

	int value = buffer->data[buffer->head];
	buffer->head = (buffer->head + 1) % MAX_BUFFER_SIZE;
	buffer->size--;

	return value;
}

void fifo_copy_to_buffer(FifoBuffer *buffer, uint8_t *outputBuffer, int *outputSize) {
	*outputSize = buffer->size;
	int index = 0;

	while (buffer->size > 0) {
		outputBuffer[index++] = fifo_pop(buffer);
		//ESP_LOGI("UART_FIFO", "Read: '%c' Index: '%d'", outputBuffer[index-1], index-1);
	}
	//ESP_LOG_BUFFER_HEXDUMP("UART_FIFO", outputBuffer, index, ESP_LOG_INFO);
}

int fifo_getSize(FifoBuffer *buffer) {
	return buffer->size;
}

uint8_t fifo_getTXSize(void) {
	uint16_t size = fifo_getSize(&txfifo);
	return (size*100)/MAX_BUFFER_SIZE;
}

void rpi_init(void) {
	fifo_init(&txfifo);

	const uart_config_t uart_config = {
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};
	// We won't use a buffer for sending data.
	uart_driver_install(RPI_UART, MAX_BUFFER_SIZE * 2, 0, 0, NULL, 0);
	uart_param_config(RPI_UART, &uart_config);
	uart_set_pin(RPI_UART, RP2040_TX_PIN, RP2040_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	// Semaphore erstellen (Binär-Semaphore für Sendebestätigung)
	tx_done_sem = xSemaphoreCreateBinary();
	send_mutex = xSemaphoreCreateMutex();
}

void sendRPi(uint16_t reg, uint16_t* data, uint16_t size) {
	xSemaphoreTake(send_mutex, portMAX_DELAY);

	while (fifo_getSize(&txfifo) > MAX_BUFFER_SIZE - ((size * 2) + 12) || gpio_get_level(UART2_RX_PIN) == 0)
	{
		printf("UART: Warte auf Puffer oder CTS HIGH\n");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	tx_disable = 1;
	//Send Header
	for(uint8_t i = 0; i < 8; i++)
	{
		fifo_push(&txfifo, header[i]);
	}

	//Send Register
	fifo_push(&txfifo, (reg>>8)); // Registerwert in den Puffer legen
	fifo_push(&txfifo, (reg&0xFF)); // Registerwert in den Puffer legen

	//Send Data Len
	fifo_push(&txfifo, (size>>8)); // Registerwert in den Puffer legen
	fifo_push(&txfifo, (size&0xFF)); // Registerwert in den Puffer legen

	for (int i = 0; i < size; i++) {
		fifo_push(&txfifo, (data[i]>>8)); // Daten in den Puffer legen
		fifo_push(&txfifo, (data[i]&0xFF)); // Daten in den Puffer legen
	}
	tx_disable = 0;
	// ⏳ Hier warten wir, bis das Senden abgeschlossen wurde
	if (tx_done_sem) {
		xSemaphoreTake(tx_done_sem, portMAX_DELAY);
	}
	xSemaphoreGive(send_mutex);
}

/**********************************************************************
 * UART Send Thread
*/
static void tx_task(void *arg)
{
	// Gesamten Pufferinhalt in ein separates Array kopieren
	int outputSize;
	uint8_t outputBuffer[MAX_BUFFER_SIZE];
	while (1) {
		if(fifo_getSize(&txfifo) > 0 && tx_disable == 0) {
			ESP_LOGD(TAG, "[UART] TX Buffer used: %d%%", fifo_getTXSize());
			led_count = 0;
			fifo_copy_to_buffer(&txfifo, outputBuffer, &outputSize);
			uart_write_bytes(RPI_UART, outputBuffer, outputSize);
			uart_wait_tx_done(RPI_UART, portMAX_DELAY);
			// ✅ Senden beendet – Semaphore freigeben
			if (tx_done_sem) {
				xSemaphoreGive(tx_done_sem);
			}
		}else {
			vTaskDelay(pdMS_TO_TICKS(10));
		}
	}
}

//Parsing for other Tasks
// Return 2 = not complete yet
// Return 1 = unknown Register
// Return 0 = parse complete
uint8_t parse_uart_cmd()
{
	if(getRxComplete())
	{
		if(getRxReg() == REG_VGA)
		{
			vga_event_t ev;
			ev.cmd = getRxData(0);
			ev.data0 = getRxData(1);
			ev.data1 = getRxData(2);
			xQueueSend(
				vga_queue,
				&ev,
				0
			);
			clearRxComplete();
			return 0;
		}
		else if(getRxReg() == REG_TOUCH)
		{
			touch_event_t ev;
			ev.x = getRxData(0);
			ev.y = getRxData(1);
			ev.pressed = getRxData(2);
			xQueueSend(
				touch_queue,
				&ev,
				0
			);
			clearRxComplete();
			return 0;
		}
		else if(getRxReg() == REG_GPIO)
		{
			gpio_event_t ev;
			ev.cmd = getRxData(0);
			ev.data0 = getRxData(1);
			xQueueSend(
				gpio_queue,
				&ev,
				0
			);
			clearRxComplete();
			return 0;
		}
		return 1;
	}
	return 2;
}

/**********************************************************************
 * UART Receive Thread
*/
static void rx_task(void *arg)
{
	int rxBytes = 0;
	uint8_t data[2];

	while (1) {
		rxBytes = uart_read_bytes(RPI_UART, data, 2, 1000 / portTICK_PERIOD_MS);
		if (rxBytes > 0) {
			led_count = 0;
			if(uart_rx_ctrl.complete == 0)
			{
				if(uart_rx_ctrl.ctrl_cnt < 4)
				{
					if(header[uart_rx_ctrl.ctrl_cnt*2] == data[0] && header[uart_rx_ctrl.ctrl_cnt*2 + 1] == data[1])
					{
						uart_rx.header[uart_rx_ctrl.ctrl_cnt] = (data[0]<<8) + data[1];
						uart_rx_ctrl.ctrl_cnt++;
					}
					else
					{
						uart_rx_ctrl.ctrl_cnt = 0;
					}
				}
				else
				{
					if(uart_rx_ctrl.ctrl_cnt == 4)
					{
						uart_rx.reg = (data[0]<<8) + data[1];
						//printf("Reg: %d\n", uart_rx.reg);
						uart_rx_ctrl.ctrl_cnt++;
					}
					else if(uart_rx_ctrl.ctrl_cnt == 5)
					{
						uart_rx.len = (data[0]<<8) + data[1];
						//printf("Len: %d\n", uart_rx.len);
						uart_rx_ctrl.ctrl_cnt++;
					}
					else if(uart_rx_ctrl.ctrl_cnt == 6)
					{
						uart_rx.data[0] = (data[0]<<8) + data[1];
						//printf("Data[0]: %d\n", uart_rx.data[0]);
						for(uint8_t i = 1; i < uart_rx.len; i++)
						{
							rxBytes = uart_read_bytes(RPI_UART, data, 2, 1000 / portTICK_PERIOD_MS);
							uart_rx.data[i] = (data[0]<<8) + data[1];
							//printf("Data[%d]: %d\n", i, uart_rx.data[i]);
						}

						uart_rx_ctrl.ctrl_cnt = 0;
						uart_rx_ctrl.complete = 1;
						gpio_set_level(UART2_TX_PIN, 0);
					}
				}
			}
			// ESP_LOGI(TAG, "Read %d bytes: '%s'", rxBytes, data);
			// ESP_LOG_BUFFER_HEXDUMP(TAG, data, rxBytes, ESP_LOG_INFO);
		}
		parse_uart_cmd();
	}
}

uint8_t getRxComplete(void)
{
	return uart_rx_ctrl.complete;
}

void clearRxComplete(void)
{
	uart_rx_ctrl.complete = 0;
	gpio_set_level(UART2_TX_PIN, 1);
}

uint16_t getRxReg(void)
{
	return uart_rx.reg;
}

uint16_t getRxLen(void)
{
	return uart_rx.len;
}

uint16_t getRxData(uint8_t index)
{
	if (index < uart_rx.len) {
		return uart_rx.data[index];
	} else {
		// Handle out-of-bounds access
		return 0; // Oder einen Fehlerwert
	}
}

/**********************************************************************
 * UART RX/TX LED Task
*/
static void rxtx_task(void *arg)
{
	while (1) {
		if(led_count < 10) {
			gpio_set_level(YELLOW_LED_PIN, 1);
			led_count++;
		}else {
			gpio_set_level(YELLOW_LED_PIN, 0);
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void rpi_uart_init(uint8_t core_num, uint8_t priority)
{
	gpio_set_direction (YELLOW_LED_PIN, GPIO_MODE_OUTPUT);
	gpio_set_level(YELLOW_LED_PIN, 0);

	gpio_set_direction (UART2_TX_PIN, GPIO_MODE_OUTPUT);
	gpio_set_direction (UART2_RX_PIN, GPIO_MODE_INPUT);
	gpio_set_level(UART2_TX_PIN, 1);
	if(priority == 0) {
		priority = configMAX_PRIORITIES-1;
	}

	gpio_queue = xQueueCreate(10, sizeof(gpio_event_t));
	touch_queue = xQueueCreate(10, sizeof(touch_event_t));
	vga_queue = xQueueCreate(20, sizeof(vga_event_t));

	rpi_init();
	xTaskCreatePinnedToCore(rx_task, "uart_rx_task", 1024*4, NULL, priority, UartRxHandle, core_num);
	xTaskCreatePinnedToCore(tx_task, "uart_tx_task", 1024*4, NULL, priority, UartTxHandle, core_num);
	xTaskCreatePinnedToCore(rxtx_task, "led_rxtx_task", 1024, NULL, priority-1, LedRxTxHandle, core_num);
}

void rpi_uart_close(void) {
	vTaskDelete(UartRxHandle);
	vTaskDelete(UartTxHandle);
	vTaskDelete(LedRxTxHandle);
}
