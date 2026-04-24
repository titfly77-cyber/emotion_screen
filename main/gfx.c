#include "gfx.h"
#include "st7789.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "GFX";

/* ===================== 内部辅助 ===================== */
#ifndef abs
#define abs(x) ((x) < 0 ? -(x) : (x))
#endif

static inline void swap_int(int *a, int *b) {
    int t = *a; *a = *b; *b = t;
}

static inline int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ===================== 基础 5x7 字体 ===================== */
/* 简化ASCII 32~127, 每个字符5列, 每列7像素用uint8_t低7位表示 */
static const uint8_t font5x7[][5] = {
    /* 32 ' ' */ {0x00,0x00,0x00,0x00,0x00},
    /* 33 '!' */ {0x00,0x00,0x5F,0x00,0x00},
    /* 34 '"' */ {0x00,0x07,0x00,0x07,0x00},
    /* 35 '#' */ {0x14,0x7F,0x14,0x7F,0x14},
    /* 36 '$' */ {0x24,0x2A,0x7F,0x2A,0x12},
    /* 37 '%' */ {0x23,0x13,0x08,0x64,0x62},
    /* 38 '&' */ {0x36,0x49,0x55,0x22,0x50},
    /* 39 ''' */ {0x00,0x05,0x03,0x00,0x00},
    /* 40 '(' */ {0x00,0x1C,0x22,0x41,0x00},
    /* 41 ')' */ {0x00,0x41,0x22,0x1C,0x00},
    /* 42 '*' */ {0x14,0x08,0x3E,0x08,0x14},
    /* 43 '+' */ {0x08,0x08,0x3E,0x08,0x08},
    /* 44 ',' */ {0x00,0x50,0x30,0x00,0x00},
    /* 45 '-' */ {0x08,0x08,0x08,0x08,0x08},
    /* 46 '.' */ {0x00,0x60,0x60,0x00,0x00},
    /* 47 '/' */ {0x20,0x10,0x08,0x04,0x02},
    /* 48 '0' */ {0x3E,0x51,0x49,0x45,0x3E},
    /* 49 '1' */ {0x00,0x42,0x7F,0x40,0x00},
    /* 50 '2' */ {0x42,0x61,0x51,0x49,0x46},
    /* 51 '3' */ {0x21,0x41,0x45,0x4B,0x31},
    /* 52 '4' */ {0x18,0x14,0x12,0x7F,0x10},
    /* 53 '5' */ {0x27,0x45,0x45,0x45,0x39},
    /* 54 '6' */ {0x3C,0x4A,0x49,0x49,0x30},
    /* 55 '7' */ {0x01,0x71,0x09,0x05,0x03},
    /* 56 '8' */ {0x36,0x49,0x49,0x49,0x36},
    /* 57 '9' */ {0x06,0x49,0x49,0x29,0x1E},
    /* 58 ':' */ {0x00,0x36,0x36,0x00,0x00},
    /* 59 ';' */ {0x00,0x56,0x36,0x00,0x00},
    /* 60 '<' */ {0x08,0x14,0x22,0x41,0x00},
    /* 61 '=' */ {0x14,0x14,0x14,0x14,0x14},
    /* 62 '>' */ {0x00,0x41,0x22,0x14,0x08},
    /* 63 '?' */ {0x02,0x01,0x51,0x09,0x06},
    /* 64 '@' */ {0x32,0x49,0x79,0x41,0x3E},
    /* 65 'A' */ {0x7E,0x11,0x11,0x11,0x7E},
    /* 66 'B' */ {0x7F,0x49,0x49,0x49,0x36},
    /* 67 'C' */ {0x3E,0x41,0x41,0x41,0x22},
    /* 68 'D' */ {0x7F,0x41,0x41,0x22,0x1C},
    /* 69 'E' */ {0x7F,0x49,0x49,0x49,0x41},
    /* 70 'F' */ {0x7F,0x09,0x09,0x09,0x01},
    /* 71 'G' */ {0x3E,0x41,0x49,0x49,0x7A},
    /* 72 'H' */ {0x7F,0x08,0x08,0x08,0x7F},
    /* 73 'I' */ {0x00,0x41,0x7F,0x41,0x00},
    /* 74 'J' */ {0x20,0x40,0x41,0x3F,0x01},
    /* 75 'K' */ {0x7F,0x08,0x14,0x22,0x41},
    /* 76 'L' */ {0x7F,0x40,0x40,0x40,0x40},
    /* 77 'M' */ {0x7F,0x02,0x0C,0x02,0x7F},
    /* 78 'N' */ {0x7F,0x04,0x08,0x10,0x7F},
    /* 79 'O' */ {0x3E,0x41,0x41,0x41,0x3E},
    /* 80 'P' */ {0x7F,0x09,0x09,0x09,0x06},
    /* 81 'Q' */ {0x3E,0x41,0x51,0x21,0x5E},
    /* 82 'R' */ {0x7F,0x09,0x19,0x29,0x46},
    /* 83 'S' */ {0x46,0x49,0x49,0x49,0x31},
    /* 84 'T' */ {0x01,0x01,0x7F,0x01,0x01},
    /* 85 'U' */ {0x3F,0x40,0x40,0x40,0x3F},
    /* 86 'V' */ {0x1F,0x20,0x40,0x20,0x1F},
    /* 87 'W' */ {0x3F,0x40,0x38,0x40,0x3F},
    /* 88 'X' */ {0x63,0x14,0x08,0x14,0x63},
    /* 89 'Y' */ {0x07,0x08,0x70,0x08,0x07},
    /* 90 'Z' */ {0x61,0x51,0x49,0x45,0x43},
};

static int font_get_index(char c) {
    if (c < 32 || c > 90) return 0; /* 不支持的字符返回空格 */
    return c - 32;
}

/* ===================== 屏幕绘图函数 ===================== */

void gfx_fill_screen(uint16_t color)
{
    gfx_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;

    /* 裁剪 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    /* 交换字节序 */
    uint16_t swapped = (color >> 8) | (color << 8);

    const int BUF_PIX = 1024;
    uint16_t buf[BUF_PIX];
    for (int i = 0; i < BUF_PIX; i++) buf[i] = swapped;

    int total = w * h;
    int sent = 0;
    while (sent < total) {
        int n = (total - sent > BUF_PIX) ? BUF_PIX : (total - sent);
        lcd_write_color_data(buf, n);
        sent += n;
    }
}

void gfx_draw_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) return;
    lcd_set_window(x, y, x, y);
    uint16_t swapped = (color >> 8) | (color << 8);
    lcd_write_color_data(&swapped, 1);
}

void gfx_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    /* Bresenham */
    int steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep)   { swap_int(&x0, &y0); swap_int(&x1, &y1); }
    if (x0 > x1) { swap_int(&x0, &x1); swap_int(&y0, &y1); }

    int dx = x1 - x0;
    int dy = abs(y1 - y0);
    int err = dx / 2;
    int ystep = (y0 < y1) ? 1 : -1;
    int yy = y0;

    for (int xx = x0; xx <= x1; xx++) {
        if (steep)
            gfx_draw_pixel(yy, xx, color);
        else
            gfx_draw_pixel(xx, yy, color);
        err -= dy;
        if (err < 0) {
            yy += ystep;
            err += dx;
        }
    }
}

void gfx_fill_circle(int cx, int cy, int r, uint16_t color)
{
    if (r <= 0) return;
    for (int yy = -r; yy <= r; yy++) {
        int half_w = (int)sqrtf((float)(r * r - yy * yy));
        gfx_fill_rect(cx - half_w, cy + yy, half_w * 2 + 1, 1, color);
    }
}

void gfx_fill_ellipse(int cx, int cy, int rx, int ry, uint16_t color)
{
    if (rx <= 0 || ry <= 0) return;
    for (int yy = -ry; yy <= ry; yy++) {
        float ratio = 1.0f - ((float)(yy * yy)) / ((float)(ry * ry));
        if (ratio < 0) ratio = 0;
        int half_w = (int)(rx * sqrtf(ratio));
        gfx_fill_rect(cx - half_w, cy + yy, half_w * 2 + 1, 1, color);
    }
}

void gfx_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    /* 排序顶点 y0 <= y1 <= y2 */
    if (y0 > y1) { swap_int(&y0, &y1); swap_int(&x0, &x1); }
    if (y1 > y2) { swap_int(&y1, &y2); swap_int(&x1, &x2); }
    if (y0 > y1) { swap_int(&y0, &y1); swap_int(&x0, &x1); }

    if (y0 == y2) {
        int lo = x0, hi = x0;
        if (x1 < lo) lo = x1; else if (x1 > hi) hi = x1;
        if (x2 < lo) lo = x2; else if (x2 > hi) hi = x2;
        gfx_fill_rect(lo, y0, hi - lo + 1, 1, color);
        return;
    }

    int total_height = y2 - y0;
    for (int i = 0; i <= total_height; i++) {
        bool second_half = (i > y1 - y0) || (y1 == y0);
        int segment_height = second_half ? (y2 - y1) : (y1 - y0);
        if (segment_height == 0) segment_height = 1;

        float a = (float)i / total_height;
        float b = (float)(i - (second_half ? (y1 - y0) : 0)) / segment_height;

        int ax = x0 + (int)((x2 - x0) * a);
        int bx = second_half ?
                 x1 + (int)((x2 - x1) * b) :
                 x0 + (int)((x1 - x0) * b);

        if (ax > bx) swap_int(&ax, &bx);
        gfx_fill_rect(ax, y0 + i, bx - ax + 1, 1, color);
    }
}

/* ===================== Sprite 实现 ===================== */

sprite_t *sprite_create(int w, int h)
{
    sprite_t *spr = (sprite_t *)malloc(sizeof(sprite_t));
    if (!spr) {
        ESP_LOGE(TAG, "sprite alloc failed");
        return NULL;
    }
    spr->w = w;
    spr->h = h;
    spr->buf = (uint16_t *)heap_caps_malloc(w * h * 2, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!spr->buf) {
        /* 回退到普通内存 */
        spr->buf = (uint16_t *)malloc(w * h * 2);
    }
    if (!spr->buf) {
        ESP_LOGE(TAG, "sprite buffer alloc failed (%dx%d)", w, h);
        free(spr);
        return NULL;
    }
    memset(spr->buf, 0, w * h * 2);
    return spr;
}

void sprite_destroy(sprite_t *spr)
{
    if (!spr) return;
    if (spr->buf) free(spr->buf);
    free(spr);
}

void sprite_fill(sprite_t *spr, uint16_t color)
{
    if (!spr || !spr->buf) return;
    int total = spr->w * spr->h;
    for (int i = 0; i < total; i++) {
        spr->buf[i] = color;
    }
}

static inline void sprite_set_pixel(sprite_t *spr, int x, int y, uint16_t color)
{
    if (x < 0 || x >= spr->w || y < 0 || y >= spr->h) return;
    spr->buf[y * spr->w + x] = color;
}

void sprite_draw_line(sprite_t *spr, int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!spr) return;
    int steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep)    { swap_int(&x0, &y0); swap_int(&x1, &y1); }
    if (x0 > x1) { swap_int(&x0, &x1); swap_int(&y0, &y1); }

    int dx = x1 - x0;
    int dy = abs(y1 - y0);
    int err = dx / 2;
    int ystep = (y0 < y1) ? 1 : -1;
    int yy = y0;

    for (int xx = x0; xx <= x1; xx++) {
        if (steep)
            sprite_set_pixel(spr, yy, xx, color);
        else
            sprite_set_pixel(spr, xx, yy, color);
        err -= dy;
        if (err < 0) {
            yy += ystep;
            err += dx;
        }
    }
}

void sprite_draw_string(sprite_t *spr, const char *str, int x, int y, int scale, uint16_t color)
{
    if (!spr || !str) return;

    /* 居中 — x,y 是中心坐标 */
    int len = strlen(str);
    int char_w = 5 * scale + scale; /* 5 像素 + 1 间距, 都乘以 scale */
    int total_w = len * char_w - scale;
    int total_h = 7 * scale;
    int sx = x - total_w / 2;
    int sy = y - total_h / 2;

    for (int c = 0; c < len; c++) {
        int idx = font_get_index(str[c]);
        for (int col = 0; col < 5; col++) {
            uint8_t line = font5x7[idx][col];
            for (int row = 0; row < 7; row++) {
                if (line & (1 << row)) {
                    /* 绘制 scale x scale 的像素块 */
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            sprite_set_pixel(spr,
                                sx + c * char_w + col * scale + dx,
                                sy + row * scale + dy,
                                color);
                        }
                    }
                }
            }
        }
    }
}

void sprite_push(sprite_t *spr, int screen_x, int screen_y)
{
    if (!spr || !spr->buf) return;
    lcd_push_image(screen_x, screen_y, spr->w, spr->h, spr->buf);
}