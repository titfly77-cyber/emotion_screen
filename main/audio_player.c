#include "audio_player.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "sdcard.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "AUDIO_PLAYER";

/* ---------- I2S 音频引脚配置 (请按实际硬件修改) ---------- */
#define I2S_BCK_PIN   3
#define I2S_WS_PIN    4
#define I2S_DO_PIN    5

static i2s_chan_handle_t tx_chan;
static volatile bool is_playing_audio = false;
static bool i2s_is_enabled = false; /* ★ 新增：记录 I2S 底层状态防报错 */

/* 引入 main.c 中已经定义好的全局缓冲池与锁 */
extern StreamBufferHandle_t audio_stream_buf;
extern FILE *sd_audio_fp;
extern SemaphoreHandle_t sd_mutex;
extern volatile bool is_downloading;

#pragma pack(push, 1)
typedef struct {
    char     riff[4];
    uint32_t file_size;
    char     wave[4];
    char     fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_header_t;
#pragma pack(pop)

void audio_player_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 1000; 
    
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100), 
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_PIN,
            .ws   = I2S_WS_PIN,
            .dout = I2S_DO_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false, },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    
    i2s_is_enabled = false; // 初始状态为关闭
    ESP_LOGI(TAG, "I2S Audio initialized (Standby mode)");
}

static void audio_play_task(void *pvParameters) {
    uint8_t *buffer = heap_caps_malloc(4096, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!buffer) {
        is_playing_audio = false;
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_written;

    while (is_playing_audio) {
        if (is_downloading) break; 
        
        size_t bytes_read = xStreamBufferReceive(audio_stream_buf, buffer, 4096, pdMS_TO_TICKS(50));
        
        if (bytes_read > 0) {
            // 软件音量衰减防破音 (右移 1 位 = 减小一半音量)
            int16_t *pcm_data = (int16_t *)buffer;
            int num_samples = bytes_read / 2;
            for (int i = 0; i < num_samples; i++) {
                pcm_data[i] = pcm_data[i] >> 1; 
            }

            i2s_channel_write(tx_chan, buffer, bytes_read, &bytes_written, portMAX_DELAY);
        } else {
            xSemaphoreTake(sd_mutex, portMAX_DELAY);
            bool eof = (sd_audio_fp == NULL);
            xSemaphoreGive(sd_mutex);
            if (eof) break; 
        }
    }

    /* 解决结束杂音的核心代码 */
    memset(buffer, 0, 4096);
    i2s_channel_write(tx_chan, buffer, 4096, &bytes_written, pdMS_TO_TICKS(100));

    // ★ 智能关闭时钟 ★
    if (i2s_is_enabled) {
        i2s_channel_disable(tx_chan);
        i2s_is_enabled = false;
    }

    free(buffer);
    is_playing_audio = false;
    ESP_LOGI(TAG, "Audio playback finished, Amp muted.");
    vTaskDelete(NULL);
}

void audio_player_play(const char* filename) {
    if (is_playing_audio) return;

    xSemaphoreTake(sd_mutex, portMAX_DELAY);
    if (sd_audio_fp) { fclose(sd_audio_fp); }
    sd_audio_fp = sdcard_fopen(filename, "r");
    
    if (sd_audio_fp) {
        wav_header_t wav_head;
        uint32_t data_offset = 44; 
        
        if (fread(&wav_head, 1, sizeof(wav_header_t), sd_audio_fp) == sizeof(wav_header_t)) {
            if (strncmp(wav_head.riff, "RIFF", 4) == 0 && strncmp(wav_head.wave, "WAVE", 4) == 0) {
                ESP_LOGI(TAG, "WAV File: %lu Hz, %d Channels, %d Bits", 
                         wav_head.sample_rate, wav_head.num_channels, wav_head.bits_per_sample);
                
                // ★ 智能关闭防报错 ★
                if (i2s_is_enabled) {
                    i2s_channel_disable(tx_chan);
                    i2s_is_enabled = false;
                }
                
                i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(wav_head.sample_rate);
                i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);

                i2s_std_slot_config_t slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                    (i2s_data_bit_width_t)wav_head.bits_per_sample, 
                    (wav_head.num_channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO
                );
                i2s_channel_reconfig_std_slot(tx_chan, &slot_cfg);
                
                fseek(sd_audio_fp, 0, SEEK_SET);
                uint8_t head_buf[256];
                int read_len = fread(head_buf, 1, 256, sd_audio_fp);
                for(int i = 12; i < read_len - 4; i++) {
                    if(head_buf[i]=='d' && head_buf[i+1]=='a' && head_buf[i+2]=='t' && head_buf[i+3]=='a') {
                        data_offset = i + 8;
                        break;
                    }
                }
            }
        }
        
        fseek(sd_audio_fp, data_offset, SEEK_SET); 
        xStreamBufferReset(audio_stream_buf);
    }
    xSemaphoreGive(sd_mutex);

    if (!sd_audio_fp) {
        ESP_LOGE(TAG, "Audio file not found: %s", filename);
        return;
    }

    /* 播放开始前，开启时钟并记录状态 */
    if (!i2s_is_enabled) {
        i2s_channel_enable(tx_chan);
        i2s_is_enabled = true;
    }

    is_playing_audio = true;
    xTaskCreatePinnedToCore(audio_play_task, "audio_play", 6144, NULL, 5, NULL, 1);
}

bool audio_player_is_playing(void) {
    return is_playing_audio;
}