#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c.h"

#define MPU6050_DEFAULT_ADDR 0x68

#define MPU6050_REG_PWR_MGMT_1   0x6B
#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_WHO_AM_I     0x75

typedef enum {
    MPU6050_ACCEL_RANGE_2G  = 0,
    MPU6050_ACCEL_RANGE_4G  = 1,
    MPU6050_ACCEL_RANGE_8G  = 2,
    MPU6050_ACCEL_RANGE_16G = 3,
} mpu6050_accel_range_t;

typedef struct {
    i2c_port_t port;
    uint8_t address;
    int sda_gpio;
    int scl_gpio;
    uint32_t clk_hz;
    mpu6050_accel_range_t accel_range;
    int dlpf_bandwidth_hz;   // 5, 10, 21, 44, 94, 184
} mpu6050_config_t;

typedef struct {
    int16_t ax, ay, az;
    int16_t temp;
    int16_t gx, gy, gz;
} mpu6050_raw_t;

typedef struct {
    float ax_g, ay_g, az_g;       // accel in g
    float gx_dps, gy_dps, gz_dps; // gyro in degrees/second
    float temp_c;
} mpu6050_sample_t;

esp_err_t mpu6050_init(const mpu6050_config_t *cfg);
esp_err_t mpu6050_read_raw(mpu6050_raw_t *out);
esp_err_t mpu6050_read(mpu6050_sample_t *out);
