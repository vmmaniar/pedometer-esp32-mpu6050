#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c.h"

#define SSD1306_DEFAULT_ADDR 0x3C
#define SSD1306_WIDTH        128
#define SSD1306_HEIGHT       64
#define SSD1306_PAGES        (SSD1306_HEIGHT / 8)
#define SSD1306_BUF_BYTES    (SSD1306_WIDTH * SSD1306_PAGES)

typedef struct {
    i2c_port_t port;
    uint8_t address;
} ssd1306_config_t;

esp_err_t ssd1306_init(const ssd1306_config_t *cfg);
void      ssd1306_clear(void);
void      ssd1306_draw_char(int col, int page, char c);
void      ssd1306_draw_string(int col, int page, const char *s);
esp_err_t ssd1306_flush(void);
