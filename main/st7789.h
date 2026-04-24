#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ===================== 引脚定义 ===================== */
#define LCD_PIN_MOSI    41
#define LCD_PIN_SCLK    40
#define LCD_PIN_CS      39
#define LCD_PIN_DC      38
#define LCD_PIN_RST     42

#define LCD_WIDTH       240
#define LCD_HEIGHT      240
#define LCD_SPI_FREQ_HZ (40 * 1000 * 1000)  /* 40 MHz */
#define LCD_ROTATION    2                     /* 与 Arduino 一致 */

/* ===================== API ===================== */
esp_err_t st7789_init(void);

/* 底层像素操作 */
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_write_color_data(const uint16_t *data, int pixel_count);
void lcd_push_image(int x, int y, int w, int h, const uint16_t *data);

/* 供 JPEG 回调使用 — data 已为大端 RGB565 */
void lcd_push_image_swapped(int x, int y, int w, int h, const uint16_t *data);