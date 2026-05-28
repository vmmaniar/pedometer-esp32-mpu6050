#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "mpu6050.h"
#include "ssd1306.h"
#include "step_detector.h"

#define SDA_GPIO 21
#define SCL_GPIO 22
#define SAMPLE_HZ 100

static const char *TAG = "pedometer";

static void persist_steps(uint32_t steps)
{
    nvs_handle_t h;
    if (nvs_open("ped", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "steps", steps);
    nvs_commit(h);
    nvs_close(h);
}

static uint32_t load_steps(void)
{
    nvs_handle_t h;
    uint32_t v = 0;
    if (nvs_open("ped", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "steps", &v);
        nvs_close(h);
    }
    return v;
}

static void draw_ui(uint32_t steps, float cadence)
{
    char line[32];
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "STEPS");
    snprintf(line, sizeof(line), "%lu", (unsigned long)steps);
    ssd1306_draw_string(0, 2, line);
    ssd1306_draw_string(0, 5, "CADENCE");
    snprintf(line, sizeof(line), "%d SPM", (int)cadence);
    ssd1306_draw_string(0, 7, line);
    ssd1306_flush();
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_LOGI(TAG, "Pedometer starting");

    mpu6050_config_t mcfg = {
        .port = I2C_NUM_0,
        .address = MPU6050_DEFAULT_ADDR,
        .sda_gpio = SDA_GPIO,
        .scl_gpio = SCL_GPIO,
        .clk_hz = 400000,
        .accel_range = MPU6050_ACCEL_RANGE_4G,
        .dlpf_bandwidth_hz = 44,
    };
    ESP_ERROR_CHECK(mpu6050_init(&mcfg));

    ssd1306_config_t scfg = { .port = I2C_NUM_0, .address = SSD1306_DEFAULT_ADDR };
    ESP_ERROR_CHECK(ssd1306_init(&scfg));

    step_cfg_t stepcfg = {
        .sample_rate_hz = (float)SAMPLE_HZ,
        .lp_cutoff_hz = 5.0f,
        .min_step_interval_ms = 250,
        .k_threshold = 1.2f,
        .min_std_g = 0.05f,
    };
    step_detector_init(&stepcfg);

    uint32_t persisted = load_steps();
    ESP_LOGI(TAG, "Loaded %u steps from NVS", (unsigned)persisted);

    const TickType_t period = pdMS_TO_TICKS(1000 / SAMPLE_HZ);
    TickType_t next = xTaskGetTickCount();
    int ui_tick = 0;
    int save_tick = 0;

    while (1) {
        mpu6050_sample_t s;
        if (mpu6050_read(&s) == ESP_OK) {
            step_state_t state = {0};
            step_detector_update(s.ax_g, s.ay_g, s.az_g, &state);
            uint32_t total = persisted + state.step_count;

            if (++ui_tick >= 25) {       // refresh ~4 Hz
                draw_ui(total, state.cadence_spm);
                ui_tick = 0;
            }
            if (++save_tick >= 6000) {   // persist every 60 s
                persist_steps(total);
                save_tick = 0;
            }
        }
        vTaskDelayUntil(&next, period);
    }
}
