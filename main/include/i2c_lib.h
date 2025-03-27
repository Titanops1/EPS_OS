#ifndef I2C_LIB
#define I2C_LIB

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "string.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

#define I2C_MASTER_NUM              0                         /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ          400000                     /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define MPU6500_SENSOR_ADDR                 0x69        /*!< Slave address of the MPU6500 sensor */

#define MPU6500_INT_PIN_CFG_REG_ADDR        0x37        /*!< Register addresses of the interrupt pin configuration register */
#define MPU6500_INT_ENABLE_REG_ADDR         0x38        /*!< Register addresses of the interrupt enable register */
#define MPU6500_INT_STATUS_REG_ADDR         0x3A        /*!< Register addresses of the interrupt status register */

#define MPU6500_ACCEL_XOUT_H_REG_ADDR       0x3B        /*!< Register addresses of the accelerometer measurements */
#define MPU6500_ACCEL_XOUT_L_REG_ADDR       0x3C
#define MPU6500_ACCEL_YOUT_H_REG_ADDR       0x3D
#define MPU6500_ACCEL_YOUT_L_REG_ADDR       0x3E
#define MPU6500_ACCEL_ZOUT_H_REG_ADDR       0x3F
#define MPU6500_ACCEL_ZOUT_L_REG_ADDR       0x40
#define MPU6500_TEMP_OUT_H_REG_ADDR         0x41        /*!< Register addresses of the temperature measurements */
#define MPU6500_TEMP_OUT_L_REG_ADDR         0x42
#define MPU6500_GYRO_XOUT_H_REG_ADDR        0x43        /*!< Register addresses of the gyroscope measurements */
#define MPU6500_GYRO_XOUT_L_REG_ADDR        0x44
#define MPU6500_GYRO_YOUT_H_REG_ADDR        0x45
#define MPU6500_GYRO_YOUT_L_REG_ADDR        0x46
#define MPU6500_GYRO_ZOUT_H_REG_ADDR        0x47
#define MPU6500_GYRO_ZOUT_L_REG_ADDR        0x48

#define MPU6500_PWR_MGMT_1_REG_ADDR         0x6B        /*!< Register addresses of the power managment register */
#define MPU6500_WHO_AM_I_REG_ADDR           0x75        /*!< Register addresses of the "who am I" register */

#define MPU6500_RESET_BIT                   7		   /*!< Bit position of the reset bit in the power managment register */

extern int16_t mpu6500_gyro_raw[3];
extern int16_t mpu6500_accel_raw[3];
extern int16_t mpu6500_temp_raw;
extern float mpu6500_accel[3];
extern float mpu6500_gyro[3];
extern float mpu6500_temp;

void i2c_init(void);
void i2c_close(void);

esp_err_t mpu6500_register_read(uint8_t reg_addr, uint8_t *data, size_t len);
esp_err_t mpu6500_register_write_byte(uint8_t reg_addr, uint8_t data);

void mpu6500_readGyroskop();

esp_err_t save_calibration(int16_t *accel_offset, int16_t *gyro_offset);
esp_err_t load_calibration(int16_t *accel_offset, int16_t *gyro_offset);
void mpu6500_apply_calibration(int16_t *raw_accel, int16_t *raw_gyro, int16_t *accel_offset, int16_t *gyro_offset);
esp_err_t mpu6500_read_accel_raw(int16_t *raw_accel);
esp_err_t mpu6500_read_gyro_raw(int16_t *raw_gyro);
void mpu6500_calibrate(int16_t *accel_offset, int16_t *gyro_offset);
void mpu6500_convert_data(int16_t *raw_accel, int16_t *raw_gyro, float *accel, float *gyro, uint8_t accel_range, uint8_t gyro_range);

#endif