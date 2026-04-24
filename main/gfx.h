#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ===================== RGB565 颜色 (主机字节序) ===================== */
#define GFX_BLACK   0x0000
#define GFX_WHITE   0xFFFF
#define GFX_RED     0xF800
#define GFX_GREEN   0x07E0
#define GFX_BLUE    0x001F
#define GFX_YELLOW  0xFFE0

/* ===================== 绘图函数 ===================== */
void gfx_fill_screen(uint16_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);
void gfx_draw_pixel(int x, int y, uint16_t color);
void gfx_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
void gfx_fill_circle(int cx, int cy, int r, uint16_t color);
void gfx_fill_ellipse(int cx, int cy, int rx, int ry, uint16_t color);
void gfx_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);

/* ===================== Sprite (小型帧缓冲) ===================== */
typedef struct {
    uint16_t *buf;       /* 主机字节序 RGB565 */
    int       w;
    int       h;
} sprite_t;

sprite_t *sprite_create(int w, int h);
void      sprite_destroy(sprite_t *spr);
void      sprite_fill(sprite_t *spr, uint16_t color);
void      sprite_draw_line(sprite_t *spr, int x0, int y0, int x1, int y1, uint16_t color);
void      sprite_draw_string(sprite_t *spr, const char *str, int x, int y, int scale, uint16_t color);
void      sprite_push(sprite_t *spr, int screen_x, int screen_y);