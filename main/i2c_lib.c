/* i2c - Simple example

   Simple I2C example that shows how to initialize I2C
   as well as reading and writing from and to registers for a sensor connected over I2C.

   The sensor used in this example is a MPU6500 inertial measurement unit.

   For other examples please check:
   https://github.com/espressif/esp-idf/tree/master/examples

   See README.md file to get detailed usage of this example.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "i2c_lib.h"
#include "pin_def.h"

#include "freertos/semphr.h"
#include "freertos/queue.h"

#define CALIBRATION_SAMPLES 1000

static const char *TAG = "i2c";

int16_t mpu6500_gyro_raw[3];
int16_t mpu6500_accel_raw[3];
int16_t mpu6500_temp_raw;

int16_t accel_offset[3];
int16_t gyro_offset[3];

float mpu6500_accel[3];
float mpu6500_gyro[3];
float mpu6500_temp;

SemaphoreHandle_t GyroMutex = NULL;

#define NVS_NAMESPACE "MPU6500"

esp_err_t save_calibration(int16_t *accel_offset, int16_t *gyro_offset) {
	nvs_handle_t handle;
	esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
	if (err != ESP_OK) return err;

	for (int i = 0; i < 3; i++) {
		char key_accel[10], key_gyro[10];
		sprintf(key_accel, "accel_%d", i);
		sprintf(key_gyro, "gyro_%d", i);

		nvs_set_i16(handle, key_accel, accel_offset[i]);
		nvs_set_i16(handle, key_gyro, gyro_offset[i]);
	}

	err = nvs_commit(handle);
	nvs_close(handle);
	return err;
}

esp_err_t load_calibration(int16_t *accel_offset, int16_t *gyro_offset) {
	nvs_handle_t handle;
	esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
	if (err != ESP_OK) return err;

	for (int i = 0; i < 3; i++) {
		char key_accel[10], key_gyro[10];
		sprintf(key_accel, "accel_%d", i);
		sprintf(key_gyro, "gyro_%d", i);

		nvs_get_i16(handle, key_accel, &accel_offset[i]);
		nvs_get_i16(handle, key_gyro, &gyro_offset[i]);
	}

	nvs_close(handle);
	return ESP_OK;
}

/**
 * @brief Read a sequence of bytes from a MPU6500 sensor registers
 */
esp_err_t mpu6500_register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
	int ret;
	xSemaphoreTake(GyroMutex, portMAX_DELAY);
	ret = i2c_master_write_read_device(I2C_MASTER_NUM, MPU6500_SENSOR_ADDR, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
	xSemaphoreGive(GyroMutex);
	return ret;
}

/**
 * @brief Write a byte to a MPU6500 sensor register
 */
esp_err_t mpu6500_register_write_byte(uint8_t reg_addr, uint8_t data)
{
	int ret;
	uint8_t write_buf[2] = {reg_addr, data};
	xSemaphoreTake(GyroMutex, portMAX_DELAY);
	ret = i2c_master_write_to_device(I2C_MASTER_NUM, MPU6500_SENSOR_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
	xSemaphoreGive(GyroMutex);
	return ret;
}

void mpu6500_apply_calibration(int16_t *raw_accel, int16_t *raw_gyro, int16_t *accel_offset, int16_t *gyro_offset) {
	for (int i = 0; i < 3; i++) {
		raw_accel[i] -= accel_offset[i];
		raw_gyro[i] -= gyro_offset[i];
	}
}

esp_err_t mpu6500_read_accel_raw(int16_t *raw_accel)
{
	uint8_t data[6];

	ESP_ERROR_CHECK(mpu6500_register_read(MPU6500_ACCEL_XOUT_H_REG_ADDR, data, 6));

	raw_accel[0] = (data[0] << 8) | data[1];
	raw_accel[1] = (data[2] << 8) | data[3];
	raw_accel[2] = (data[4] << 8) | data[5];

	return ESP_OK;
}

esp_err_t mpu6500_read_gyro_raw(int16_t *raw_gyro)
{
	uint8_t data[6];

	ESP_ERROR_CHECK(mpu6500_register_read(MPU6500_GYRO_XOUT_H_REG_ADDR, data, 6));

	raw_gyro[0] = (data[0] << 8) | data[1];
	raw_gyro[1] = (data[2] << 8) | data[3];
	raw_gyro[2] = (data[4] << 8) | data[5];

	return ESP_OK;
}

void mpu6500_calibrate(int16_t *accel_offset, int16_t *gyro_offset) {
	int32_t accel_sum[3] = {0, 0, 0};
	int32_t gyro_sum[3] = {0, 0, 0};
	int16_t raw_accel[3], raw_gyro[3];

	// Mehrere Messwerte sammeln
	for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
		mpu6500_read_accel_raw(raw_accel);
		mpu6500_read_gyro_raw(raw_gyro);

		for (int j = 0; j < 3; j++) {
			accel_sum[j] += raw_accel[j];
			gyro_sum[j] += raw_gyro[j];
		}
		vTaskDelay(5 / portTICK_PERIOD_MS); // Kurze Pause zwischen den Messungen
	}

	// Durchschnitt berechnen und als Offset speichern
	for (int j = 0; j < 3; j++) {
		accel_offset[j] = accel_sum[j] / CALIBRATION_SAMPLES;
		gyro_offset[j] = gyro_sum[j] / CALIBRATION_SAMPLES;
	}

	// Z-Achse des Beschleunigungssensors korrigieren (Erwartet: 1g → 16384 LSB bei ±2g)
	accel_offset[2] -= 16384;

	ESP_LOGI("MPU6500", "Calibration Done. Offsets:");
	ESP_LOGI("MPU6500", "Accel: X=%d Y=%d Z=%d", accel_offset[0], accel_offset[1], accel_offset[2]);
	ESP_LOGI("MPU6500", "Gyro:  X=%d Y=%d Z=%d", gyro_offset[0], gyro_offset[1], gyro_offset[2]);
}

void mpu6500_convert_data(int16_t *raw_accel, int16_t *raw_gyro, float *accel, float *gyro, uint8_t accel_range, uint8_t gyro_range) {
	float accel_sensitivity;
	float gyro_sensitivity;

	switch (accel_range) {
		case 0: accel_sensitivity = 16384.0; break; // ±2g
		case 1: accel_sensitivity = 8192.0; break;  // ±4g
		case 2: accel_sensitivity = 4096.0; break;  // ±8g
		case 3: accel_sensitivity = 2048.0; break;  // ±16g
		default: accel_sensitivity = 16384.0; break;
	}

	switch (gyro_range) {
		case 0: gyro_sensitivity = 131.0; break;  // ±250°/s
		case 1: gyro_sensitivity = 65.5; break;   // ±500°/s
		case 2: gyro_sensitivity = 32.8; break;   // ±1000°/s
		case 3: gyro_sensitivity = 16.4; break;   // ±2000°/s
		default: gyro_sensitivity = 131.0; break;
	}

	for (int i = 0; i < 3; i++) {
		mpu6500_accel[i] = raw_accel[i] / accel_sensitivity;
		mpu6500_gyro[i] = raw_gyro[i] / gyro_sensitivity;
	}
}

void mpu6500_readGyroskop()
{
	//ESP_LOGI("MPU6500", "Reading Gyro...");
	ESP_ERROR_CHECK(mpu6500_read_accel_raw(mpu6500_accel_raw));
	ESP_ERROR_CHECK(mpu6500_read_gyro_raw(mpu6500_gyro_raw));
	//ESP_LOGI("MPU6500", "Applying calibration...");
	mpu6500_apply_calibration(mpu6500_accel_raw, mpu6500_gyro_raw, accel_offset, gyro_offset);
	mpu6500_convert_data(mpu6500_accel_raw, mpu6500_gyro_raw, mpu6500_accel, mpu6500_gyro, 0, 0); // 0 = ±2g, 0 = ±250°/s
	//ESP_LOGI("MPU6500", "ACCEL_X = %.2fg, ACCEL_Y = %.2fg, ACCEL_Z = %.2fg", mpu6500_accel[0], mpu6500_accel[1], mpu6500_accel[2]);
	//ESP_LOGI("MPU6500", "GYRO_X = %.2f°/s, GYRO_Y = %.2f°/s, GYRO_Z = %.2f°/s", mpu6500_gyro[0], mpu6500_gyro[1], mpu6500_gyro[2]);
}

/**
 * @brief i2c master initialization
 */
static esp_err_t i2c_master_init(void)
{
	int i2c_master_port = I2C_MASTER_NUM;

	i2c_config_t conf = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = I2C_SDA_PIN,
		.scl_io_num = I2C_SCL_PIN,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = I2C_MASTER_FREQ_HZ,
	};

	i2c_param_config(i2c_master_port, &conf);

	return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}


void i2c_init()
{
	uint8_t data[2];
	ESP_ERROR_CHECK(i2c_master_init());
	ESP_LOGI(TAG, "I2C initialized successfully");

	GyroMutex = xSemaphoreCreateBinary();
	if (GyroMutex == NULL) {
		ESP_LOGE(TAG, "Failed to create GyroMutex semaphore");
		return;
	}
	if(xSemaphoreGive(GyroMutex) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to give GyroMutex semaphore");
		return;
	}

	/* Demonstrate writing by reseting the MPU6500 */
	ESP_ERROR_CHECK(mpu6500_register_write_byte(MPU6500_PWR_MGMT_1_REG_ADDR, 1 << MPU6500_RESET_BIT));
	vTaskDelay(pdMS_TO_TICKS(100));
	/* Read the MPU6500 WHO_AM_I register, on power up the register should have the value 0x71 */
	ESP_ERROR_CHECK(mpu6500_register_read(MPU6500_WHO_AM_I_REG_ADDR, data, 1));
	if(data[0] != 0x70) {
		ESP_LOGE(TAG, "MPU6500 WHO_AM_I Register returned an unexpected value: %X", data[0]);
		i2c_close();
		return;
	}
	else {
		ESP_LOGI(TAG, "Found MPU6500 sensor with WHO_AM_I value: %X", data[0]);
	}

	// Kalibrierwerte laden (falls vorhanden)
	if (load_calibration(accel_offset, gyro_offset) != ESP_OK) {
		ESP_LOGW("MPU6500", "No calibration data found, calibrating...");
		mpu6500_calibrate(accel_offset, gyro_offset);
		save_calibration(accel_offset, gyro_offset);
	}
	else
	{
		ESP_LOGI("MPU6500", "Calibration data loaded:");
		ESP_LOGI("MPU6500", "Accel: X=%d Y=%d Z=%d", accel_offset[0], accel_offset[1], accel_offset[2]);
		ESP_LOGI("MPU6500", "Gyro:  X=%d Y=%d Z=%d", gyro_offset[0], gyro_offset[1], gyro_offset[2]);
	}
}

void i2c_close()
{
	ESP_ERROR_CHECK(i2c_driver_delete(I2C_MASTER_NUM));
	ESP_LOGI(TAG, "I2C de-initialized successfully");
}