#include "st7789.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "ST7789";

/* SPI 句柄 */
static spi_device_handle_t spi_dev = NULL;

/* 根据旋转确定的列/行偏移 */
static uint16_t col_offset = 0;
static uint16_t row_offset = 0;

/* ===================== 底层 SPI 通信 ===================== */
static void lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &cmd,
    };
    gpio_set_level(LCD_PIN_DC, 0);
    spi_device_polling_transmit(spi_dev, &t);
}

static void lcd_data(const uint8_t *data, int len)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
    };
    gpio_set_level(LCD_PIN_DC, 1);
    spi_device_polling_transmit(spi_dev, &t);
}

static void lcd_data_byte(uint8_t val)
{
    lcd_data(&val, 1);
}

/* ===================== 初始化序列 ===================== */
static void st7789_hw_reset(void)
{
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void st7789_init_sequence(void)
{
    lcd_cmd(0x01); /* SWRESET */
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0x11); /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0x3A); /* COLMOD */
    lcd_data_byte(0x55); /* 16-bit RGB565 */

    /* MADCTL — 依据旋转 */
    lcd_cmd(0x36);
    switch (LCD_ROTATION) {
        case 0:
            lcd_data_byte(0x00);
            col_offset = 0; row_offset = 0;
            break;
        case 1:
            lcd_data_byte(0x60);
            col_offset = 0; row_offset = 0;
            break;
        case 2:
            lcd_data_byte(0xC0);
            col_offset = 0; row_offset = 0;
            break;
        case 3:
            lcd_data_byte(0xA0);
            col_offset = 0; row_offset = 0;
            break;
        default:
            lcd_data_byte(0x00);
            col_offset = 0; row_offset = 0;
            break;
    }

    /* INVON — 原始代码使能反转 */
    lcd_cmd(0x21);

    /* Porch control */
    lcd_cmd(0xB2);
    {
        uint8_t d[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
        lcd_data(d, sizeof(d));
    }

    /* Gate control */
    lcd_cmd(0xB7);
    lcd_data_byte(0x35);

    /* VCOM */
    lcd_cmd(0xBB);
    lcd_data_byte(0x19);

    /* LCM */
    lcd_cmd(0xC0);
    lcd_data_byte(0x2C);

    /* VDV VRH enable */
    lcd_cmd(0xC2);
    lcd_data_byte(0x01);

    /* VRH */
    lcd_cmd(0xC3);
    lcd_data_byte(0x12);

    /* VDV */
    lcd_cmd(0xC4);
    lcd_data_byte(0x20);

    /* Frame rate */
    lcd_cmd(0xC6);
    lcd_data_byte(0x0F);

    /* Power control */
    lcd_cmd(0xD0);
    {
        uint8_t d[] = {0xA4, 0xA1};
        lcd_data(d, sizeof(d));
    }

    /* Positive gamma */
    lcd_cmd(0xE0);
    {
        uint8_t d[] = {0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,
                       0x54,0x4C,0x18,0x0D,0x0B,0x1F,0x23};
        lcd_data(d, sizeof(d));
    }

    /* Negative gamma */
    lcd_cmd(0xE1);
    {
        uint8_t d[] = {0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,
                       0x44,0x51,0x2F,0x1F,0x1F,0x20,0x23};
        lcd_data(d, sizeof(d));
    }

    lcd_cmd(0x29); /* DISPON */
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* ===================== 公共 API ===================== */
esp_err_t st7789_init(void)
{
    /* GPIO 配置 */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << LCD_PIN_DC) | (1ULL << LCD_PIN_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);

    /* SPI 总线初始化 */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = LCD_PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = LCD_PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    /* SPI 设备 */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = LCD_SPI_FREQ_HZ,
        .mode           = 0,
        .spics_io_num   = LCD_PIN_CS,
        .queue_size     = 7,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi_dev));

    st7789_hw_reset();
    st7789_init_sequence();

    ESP_LOGI(TAG, "ST7789 初始化完成 (%dx%d, rotation=%d)", LCD_WIDTH, LCD_HEIGHT, LCD_ROTATION);
    return ESP_OK;
}

void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 += col_offset; x1 += col_offset;
    y0 += row_offset; y1 += row_offset;

    lcd_cmd(0x2A); /* CASET */
    {
        uint8_t d[] = { (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
                        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF) };
        lcd_data(d, 4);
    }

    lcd_cmd(0x2B); /* RASET */
    {
        uint8_t d[] = { (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
                        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF) };
        lcd_data(d, 4);
    }

    lcd_cmd(0x2C); /* RAMWR */
}

/* ===================== 使用 DMA 的绘图函数 ===================== */
void lcd_write_color_data(const uint16_t *data, int pixel_count)
{
    gpio_set_level(LCD_PIN_DC, 1);

    const int MAX_CHUNK = 32768; /* 每次最大 DMA 传输字节数 */
    const uint8_t *ptr = (const uint8_t *)data;
    int remaining = pixel_count * 2;

    while (remaining > 0) {
        int chunk = (remaining > MAX_CHUNK) ? MAX_CHUNK : remaining;
        spi_transaction_t t = {
            .length    = chunk * 8,
            .tx_buffer = ptr,
        };
        /* 这里使用 transmit 代替 polling_transmit，释放 CPU */
        spi_device_transmit(spi_dev, &t); 
        ptr       += chunk;
        remaining -= chunk;
    }
}

void lcd_push_image(int x, int y, int w, int h, const uint16_t *data)
{
    if (w <= 0 || h <= 0) return;
    lcd_set_window(x, y, x + w - 1, y + h - 1);

    int total = w * h;
    /* 使用临时缓冲做字节交换, 分批发送 */
    const int BUF_PIX = 1024;
    uint16_t buf[BUF_PIX];
    int sent = 0;

    gpio_set_level(LCD_PIN_DC, 1);
    while (sent < total) {
        int n = (total - sent > BUF_PIX) ? BUF_PIX : (total - sent);
        for (int i = 0; i < n; i++) {
            uint16_t c = data[sent + i];
            buf[i] = (c >> 8) | (c << 8);
        }
        spi_transaction_t t = {
            .length    = n * 16,
            .tx_buffer = buf,
        };
        spi_device_transmit(spi_dev, &t);
        sent += n;
    }
}

void lcd_push_image_swapped(int x, int y, int w, int h, const uint16_t *data)
{
    if (w <= 0 || h <= 0) return;
    lcd_set_window(x, y, x + w - 1, y + h - 1);
    lcd_write_color_data(data, w * h);
}