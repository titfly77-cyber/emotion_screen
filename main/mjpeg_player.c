#include "mjpeg_player.h"
#include "st7789.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/stream_buffer.h"

extern StreamBufferHandle_t video_stream_buf;
static const char *TAG = "MJPEG";

static uint8_t *read_buf       = NULL;
static uint8_t *decode_buf     = NULL;
static bool     ready          = false;
static bool     playing        = false;
static uint32_t frame_count    = 0;
static int64_t  start_us       = 0;
static int64_t  next_frame_us  = 0;

static int video_w = 0;
static int video_h = 0;

static inline int64_t now_us(void) { return esp_timer_get_time(); }

static int read_one_jpeg_frame(void)
{
    int pos = 0;
    uint8_t prev = 0;
    bool found_soi = false;
    uint8_t chunk[512]; 

    while (1) {
        int n = xStreamBufferReceive(video_stream_buf, chunk, sizeof(chunk), pdMS_TO_TICKS(10));
        if (n <= 0) return -1; 

        for (int i = 0; i < n; i++) {
            uint8_t c = chunk[i];
            if (!found_soi) {
                if (prev == 0xFF && c == 0xD8) {
                    found_soi = true;
                    read_buf[0] = 0xFF;
                    read_buf[1] = 0xD8;
                    pos = 2;
                }
            } else {
                if (pos < MJPEG_BUFFER_SIZE) {
                    read_buf[pos++] = c;
                } else {
                    return -1;
                }
                if (prev == 0xFF && c == 0xD9) {
                    return pos;
                }
            }
            prev = c;
        }
    }
    return -1;
}

static bool decode_and_display(int jpeg_size)
{
    esp_jpeg_image_cfg_t jpeg_cfg = {
        .indata      = read_buf,
        .indata_size = (size_t)jpeg_size,
        .outbuf      = decode_buf,
        .outbuf_size = (size_t)(LCD_WIDTH * LCD_HEIGHT * 2),
        .out_format  = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale   = JPEG_IMAGE_SCALE_0,
        .flags = { .swap_color_bytes = 1 },
    };

    esp_jpeg_image_output_t out_info;
    esp_err_t ret = esp_jpeg_decode(&jpeg_cfg, &out_info);
    if (ret != ESP_OK) return false;

    if (video_w == 0) {
        video_w = out_info.width;
        video_h = out_info.height;
    }

    lcd_push_image_swapped(0, 0, out_info.width, out_info.height, (const uint16_t *)decode_buf);
    return true;
}

bool mjpeg_player_init(void)
{
    if (read_buf == NULL) {
        read_buf = (uint8_t *)heap_caps_malloc(MJPEG_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!read_buf) read_buf = (uint8_t *)malloc(MJPEG_BUFFER_SIZE);
    }
    
    if (decode_buf == NULL) {
        size_t decode_size = LCD_WIDTH * LCD_HEIGHT * 2;
        decode_buf = (uint8_t *)heap_caps_malloc(decode_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!decode_buf) decode_buf = (uint8_t *)malloc(decode_size);
    }
    
    if (!read_buf || !decode_buf) {
        ESP_LOGE(TAG, "Failed to allocate MJPEG buffers!");
        return false;
    }

    video_w = 0; 
    video_h = 0;
    ready   = true;
    playing = false;
    return true;
}

void mjpeg_player_switch_video(void)
{
    if (!ready) return;
    start_us      = now_us();
    frame_count   = 0;
    next_frame_us = start_us;
    playing       = true;
    
    // 强制清理可能残留的半截帧，防止状态机死锁
    if (read_buf) {
        read_buf[0] = 0; 
        read_buf[1] = 0;
    }
}

void mjpeg_player_deinit(void)
{
    playing = false; ready = false;
    if (read_buf)   { free(read_buf);   read_buf   = NULL; }
    if (decode_buf) { free(decode_buf); decode_buf = NULL; }
}

void mjpeg_player_start(void)
{
    if (!ready) return;
    start_us      = now_us();
    frame_count   = 0;
    next_frame_us = start_us;
    playing       = true;
}

void mjpeg_player_stop(void) { playing = false; }

bool mjpeg_player_play_frame(void)
{
    if (!playing || !ready) return false;

    int64_t current = now_us();
    if (current < next_frame_us) return false;

    int jpeg_size = read_one_jpeg_frame();
    if (jpeg_size > 0) {
        decode_and_display(jpeg_size);
        frame_count++;
        next_frame_us = start_us + (int64_t)frame_count * 1000000LL / MJPEG_VIDEO_FPS;
        return true;
    } else {
        // 如果池子里没水，时间轴稍微往后推延
        next_frame_us = current + (1000000LL / MJPEG_VIDEO_FPS);
        return false;
    }
}

bool mjpeg_player_is_ready(void) { return ready; }
bool mjpeg_player_is_playing(void) { return playing; }