#include "mpu6050.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mpu6050";

static i2c_port_t s_port;
static uint8_t s_addr;
static float s_accel_lsb_per_g;
static const float GYRO_LSB_PER_DPS = 131.0f; // ±250 dps default

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(s_port, s_addr, buf, 2, pdMS_TO_TICKS(50));
}

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_write_read_device(s_port, s_addr, &reg, 1, buf, len, pdMS_TO_TICKS(50));
}

static uint8_t dlpf_for_bw(int hz)
{
    if (hz <= 5)  return 6;
    if (hz <= 10) return 5;
    if (hz <= 21) return 4;
    if (hz <= 44) return 3;
    if (hz <= 94) return 2;
    return 1;
}

esp_err_t mpu6050_init(const mpu6050_config_t *cfg)
{
    s_port = cfg->port;
    s_addr = cfg->address ? cfg->address : MPU6050_DEFAULT_ADDR;

    i2c_config_t i2c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = cfg->sda_gpio,
        .scl_io_num = cfg->scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = cfg->clk_hz ? cfg->clk_hz : 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(s_port, &i2c));
    esp_err_t err = i2c_driver_install(s_port, i2c.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    uint8_t who = 0;
    ESP_ERROR_CHECK(reg_read(MPU6050_REG_WHO_AM_I, &who, 1));
    if (who != 0x68 && who != 0x70 && who != 0x72) {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I 0x%02x", who);
        return ESP_ERR_NOT_FOUND;
    }

    // wake from sleep, use gyro x as clock source
    ESP_ERROR_CHECK(reg_write(MPU6050_REG_PWR_MGMT_1, 0x01));
    vTaskDelay(pdMS_TO_TICKS(10));

    // Sample-rate = 1 kHz / (1 + SMPLRT_DIV); pick 9 -> 100 Hz
    ESP_ERROR_CHECK(reg_write(MPU6050_REG_SMPLRT_DIV, 9));
    ESP_ERROR_CHECK(reg_write(MPU6050_REG_CONFIG, dlpf_for_bw(cfg->dlpf_bandwidth_hz)));
    ESP_ERROR_CHECK(reg_write(MPU6050_REG_GYRO_CONFIG, 0x00));     // ±250 dps
    ESP_ERROR_CHECK(reg_write(MPU6050_REG_ACCEL_CONFIG, (uint8_t)cfg->accel_range << 3));

    switch (cfg->accel_range) {
        case MPU6050_ACCEL_RANGE_2G:  s_accel_lsb_per_g = 16384.0f; break;
        case MPU6050_ACCEL_RANGE_4G:  s_accel_lsb_per_g = 8192.0f;  break;
        case MPU6050_ACCEL_RANGE_8G:  s_accel_lsb_per_g = 4096.0f;  break;
        case MPU6050_ACCEL_RANGE_16G: s_accel_lsb_per_g = 2048.0f;  break;
    }

    ESP_LOGI(TAG, "MPU-6050 ready (range=%d, dlpf=%d Hz)",
             cfg->accel_range, cfg->dlpf_bandwidth_hz);
    return ESP_OK;
}

esp_err_t mpu6050_read_raw(mpu6050_raw_t *out)
{
    uint8_t buf[14];
    esp_err_t err = reg_read(MPU6050_REG_ACCEL_XOUT_H, buf, sizeof(buf));
    if (err != ESP_OK) return err;
    out->ax   = (int16_t)((buf[0]  << 8) | buf[1]);
    out->ay   = (int16_t)((buf[2]  << 8) | buf[3]);
    out->az   = (int16_t)((buf[4]  << 8) | buf[5]);
    out->temp = (int16_t)((buf[6]  << 8) | buf[7]);
    out->gx   = (int16_t)((buf[8]  << 8) | buf[9]);
    out->gy   = (int16_t)((buf[10] << 8) | buf[11]);
    out->gz   = (int16_t)((buf[12] << 8) | buf[13]);
    return ESP_OK;
}

esp_err_t mpu6050_read(mpu6050_sample_t *out)
{
    mpu6050_raw_t raw;
    esp_err_t err = mpu6050_read_raw(&raw);
    if (err != ESP_OK) return err;
    out->ax_g = raw.ax / s_accel_lsb_per_g;
    out->ay_g = raw.ay / s_accel_lsb_per_g;
    out->az_g = raw.az / s_accel_lsb_per_g;
    out->gx_dps = raw.gx / GYRO_LSB_PER_DPS;
    out->gy_dps = raw.gy / GYRO_LSB_PER_DPS;
    out->gz_dps = raw.gz / GYRO_LSB_PER_DPS;
    out->temp_c = raw.temp / 340.0f + 36.53f;
    return ESP_OK;
}
