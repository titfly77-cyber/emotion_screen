#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

/* --- WiFi 和 NVS 所需头文件 --- */
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"

/* --- 模块头文件 --- */
#include "st7789.h"
#include "gfx.h"
#include "qmi8658.h"
#include "sdcard.h"
#include "mjpeg_player.h"
#include "rc522.h" 
#include "audio_player.h" 

static const char *TAG = "MAIN";

/* ================================================================
 * ↓↓↓ 所有可调参数集中在这里 ↓↓↓
 * ================================================================ */

#define EXAMPLE_ESP_WIFI_SSID      "waveshare_esp32"
#define EXAMPLE_ESP_WIFI_PASSWORD  "wav123456"
#define EXAMPLE_ESP_WIFI_CHANNEL   1
#define EXAMPLE_MAX_STA_CONN       4
#define TCP_FILE_PORT              3333

/* ★★★ 新增：NFC 卡片 UID 与 音频文件的映射绑定 ★★★ */
/* 请查看终端日志打印的 UID，将这里的 16 进制数字替换为你真实的卡片 UID */
#define UID_CARD_1 {0x63, 0x18, 0x29, 0x07}
#define UID_CARD_2 {0xF2, 0xE4, 0xC0, 0x01}
#define UID_CARD_3 {0x94, 0xFA, 0x0E, 0x07}
#define UID_CARD_4 {0xFA, 0xD3, 0xB3, 0x02}

#define AUDIO_FILE_1 "/s1.wav"
#define AUDIO_FILE_2 "/s2.wav"
#define AUDIO_FILE_3 "/s3.wav"
#define AUDIO_FILE_4 "/s4.wav"

/* ---------- 其他参数 ---------- */
#define CENTER_X                120
#define CENTER_Y                120
#define EYE_WIDTH               30
#define MAX_EYE_HEIGHT          50
#define EYE_SPACING             55

#define BLINK_SPEED             6.0f
#define BLINK_MIN_INTERVAL_MS   3000
#define BLINK_RANDOM_MS         2000

#define FILTER_ALPHA            0.1f

#define INVERT_TRIGGER_AY       (-8.25f)     
#define INVERT_RELEASE_AY       (-5.0f)     
#define INVERT_HOLD_MS          100         

#define SHAKE_ACCEL_THRESHOLD   50.0f       
#define SHAKE_DURATION_MS       2500        

#define SD_READ_CHUNK           (64 * 1024)   
#define STREAM_BUF_SIZE         (128 * 1024)  

/* ================================================================
 * ↑↑↑ 可调参数结束 ↑↑↑
 * ================================================================ */

/* ===================== 跨文件共享的全局变量 (去掉了 static) ===================== */
volatile bool is_downloading = false;

StreamBufferHandle_t video_stream_buf = NULL;
StreamBufferHandle_t audio_stream_buf = NULL;

FILE *sd_video_fp = NULL;
FILE *sd_audio_fp = NULL;
SemaphoreHandle_t sd_mutex = NULL;

/* ===================== 本地全局变量 ===================== */
static bool  video_ready  = false;

static float current_eye_height = MAX_EYE_HEIGHT;
static int   blink_direction    = 0;
static uint32_t last_blink_time = 0;

static uint32_t shake_start_time  = 0;
static uint32_t invert_timer      = 0;
static bool   is_shaken    = false;

static float filtered_ay = 0;

uint8_t mac[6];

typedef enum {
    STATE_NORMAL      = 0,  
    STATE_INVERTED_X  = 1,  
    STATE_SHAKING     = 2,  
    STATE_DOWNLOADING = 10,
} bot_state_t;

static int  current_state      = STATE_NORMAL;
static int  last_state         = -1;
static bool static_frame_drawn = false;
static float last_drawn_height = -1;

/* ===================== SD读取核心任务 ===================== */
static void sd_read_task(void *pvParameters) {
    uint8_t *chunk_buf = heap_caps_malloc(SD_READ_CHUNK, MALLOC_CAP_SPIRAM);
    if (!chunk_buf) vTaskDelete(NULL);

    while (1) {
        if (is_downloading) {
            vTaskDelay(pdMS_TO_TICKS(100)); 
            continue;
        }

        bool read_active = false;
        xSemaphoreTake(sd_mutex, portMAX_DELAY);

        /* 1. 视频抽水 */
        if (sd_video_fp) {
            if (xStreamBufferSpacesAvailable(video_stream_buf) >= SD_READ_CHUNK) {
                size_t bytes = fread(chunk_buf, 1, SD_READ_CHUNK, sd_video_fp);
                if (bytes > 0) {
                    xStreamBufferSend(video_stream_buf, chunk_buf, bytes, portMAX_DELAY);
                    read_active = true;
                } else {
                    fseek(sd_video_fp, 0, SEEK_SET); 
                }
            }
        }

        /* 2. 音频抽水 */
        if (sd_audio_fp && audio_player_is_playing()) {
            if (xStreamBufferSpacesAvailable(audio_stream_buf) >= SD_READ_CHUNK) {
                size_t bytes = fread(chunk_buf, 1, SD_READ_CHUNK, sd_audio_fp);
                if (bytes > 0) {
                    xStreamBufferSend(audio_stream_buf, chunk_buf, bytes, portMAX_DELAY);
                    read_active = true;
                } else {
                    fclose(sd_audio_fp);
                    sd_audio_fp = NULL;
                }
            }
        }

        xSemaphoreGive(sd_mutex);

        if (read_active) vTaskDelay(pdMS_TO_TICKS(1)); 
        else vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

/* ===================== 辅助与视频控制 ===================== */
static inline uint32_t millis_now(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static inline int random_range(int max_val) { return (int)(esp_random() % (uint32_t)max_val); }

static void play_random_video(int min_v, int max_v) {
    int v_idx = min_v + random_range(max_v - min_v + 1);
    char fname[32];
    snprintf(fname, sizeof(fname), "/v%d.mjpeg", v_idx);
    
    xSemaphoreTake(sd_mutex, portMAX_DELAY);
    if (sd_video_fp) fclose(sd_video_fp);
    sd_video_fp = sdcard_fopen(fname, "r");
    xStreamBufferReset(video_stream_buf); 
    xSemaphoreGive(sd_mutex);

    if (sd_video_fp) {
        video_ready = true;
        mjpeg_player_switch_video(); 
        ESP_LOGI(TAG, "Switched to video: %s", fname);
    } else {
        video_ready = false;
        ESP_LOGW(TAG, "Failed to open video: %s", fname);
    }
}

/* ===================== WiFi 与 TCP ===================== */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "station join");
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "station leave");
    }
}

void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASSWORD,
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .required = true, },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void tcp_server_task(void *pvParameters)
{
    uint8_t *rx_buffer = (uint8_t *)heap_caps_malloc(4096, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!rx_buffer) { vTaskDelete(NULL); return; }

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) { free(rx_buffer); vTaskDelete(NULL); return; }

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TCP_FILE_PORT);

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    listen(listen_sock, 1);

    while (1) {
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) continue;

        is_downloading = true;
        vTaskDelay(pdMS_TO_TICKS(150)); 

        char filename[64] = {0};
        int  name_len = 0;
        bool header_parsed = false;
        FILE *f = NULL;
        int len;

        while ((len = recv(sock, rx_buffer, 4096, 0)) > 0) {
            if (!header_parsed) {
                int data_start = 0;
                for (int i = 0; i < len; i++) {
                    if (rx_buffer[i] == '\n') {
                        header_parsed = true; data_start = i + 1; break;
                    } else if (name_len < sizeof(filename) - 1 && rx_buffer[i] != '\r') {
                        filename[name_len++] = rx_buffer[i];
                    }
                }
                if (header_parsed) {
                    filename[name_len] = '\0'; 
                    char full_path[128];
                    snprintf(full_path, sizeof(full_path), "/%s", filename[0] == '/' ? filename + 1 : filename);
                    f = sdcard_fopen(full_path, "w");
                    if (!f) break; 
                    if (len > data_start) fwrite(rx_buffer + data_start, 1, len - data_start, f);
                }
            } else {
                if (f) fwrite(rx_buffer, 1, len, f);
            }
            vTaskDelay(pdMS_TO_TICKS(2)); 
        }

        if (f) fclose(f);
        close(sock); 
        is_downloading = false; 
    }
}

/* ===================== RFID 独立扫描任务 ===================== */
static void rc522_task(void *pvParameters)
{
    // 把上面定义的宏转换成数组，方便后面使用 memcmp 比对
    uint8_t target_uid1[4] = UID_CARD_1;
    uint8_t target_uid2[4] = UID_CARD_2;
    uint8_t target_uid3[4] = UID_CARD_3;
    uint8_t target_uid4[4] = UID_CARD_4;

    while (1) {
        if (!is_downloading) {
            uint8_t uid[4];
            if (rc522_read_uid(uid)) {
                ESP_LOGI(TAG, "Card detected! UID: %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
                
                // 只有当当前没有音频在播放时，才允许切歌
                if (!audio_player_is_playing()) {
                    const char *file_to_play = NULL;

                    /* 开始比对卡片 UID */
                    if (memcmp(uid, target_uid1, 4) == 0) {
                        file_to_play = AUDIO_FILE_1;
                    } else if (memcmp(uid, target_uid2, 4) == 0) {
                        file_to_play = AUDIO_FILE_2;
                    } else if (memcmp(uid, target_uid3, 4) == 0) {
                        file_to_play = AUDIO_FILE_3;
                    } else if (memcmp(uid, target_uid4, 4) == 0) {
                        file_to_play = AUDIO_FILE_4;
                    } else {
                        ESP_LOGW(TAG, "Unknown Card, no audio mapped.");
                    }

                    // 如果匹配到了有效路径，就开始播放
                    if (file_to_play != NULL) {
                        ESP_LOGI(TAG, "Playing mapped audio: %s", file_to_play);
                        audio_player_play(file_to_play);
                    }

                    // 防抖动：读到卡片后强制休眠 1 秒，防止卡没拿走导致疯狂重复触发
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ===================== 降级绘图函数 ===================== */
static void draw_downloading(void) {
    gfx_fill_screen(GFX_BLACK);
    gfx_fill_rect(CENTER_X - 10, CENTER_Y - 40, 20, 50, GFX_BLUE);
    gfx_fill_triangle(CENTER_X - 30, CENTER_Y + 10, CENTER_X + 30, CENTER_Y + 10, CENTER_X, CENTER_Y + 50, GFX_BLUE);
}

static void draw_eyes_smooth(float h) {
    if (fabsf(h - last_drawn_height) < 0.5f) return;
    uint16_t color = GFX_YELLOW;
    if (h < last_drawn_height) {
        int delta = (int)ceilf(last_drawn_height - h);
        gfx_fill_rect(CENTER_X - EYE_SPACING - EYE_WIDTH, CENTER_Y - (int)last_drawn_height, EYE_WIDTH * 2, delta + 2, GFX_BLACK);
        gfx_fill_rect(CENTER_X + EYE_SPACING - EYE_WIDTH, CENTER_Y - (int)last_drawn_height, EYE_WIDTH * 2, delta + 2, GFX_BLACK);
        gfx_fill_rect(CENTER_X - EYE_SPACING - EYE_WIDTH, CENTER_Y + (int)h, EYE_WIDTH * 2, delta + 2, GFX_BLACK);
        gfx_fill_rect(CENTER_X + EYE_SPACING - EYE_WIDTH, CENTER_Y + (int)h, EYE_WIDTH * 2, delta + 2, GFX_BLACK);
    }
    if (h > 4) {
        gfx_fill_ellipse(CENTER_X - EYE_SPACING, CENTER_Y, EYE_WIDTH, (int)h, color);
        gfx_fill_ellipse(CENTER_X + EYE_SPACING, CENTER_Y, EYE_WIDTH, (int)h, color);
    } else {
        gfx_fill_rect(CENTER_X - EYE_SPACING - 25, CENTER_Y - 2, 50, 4, color);
        gfx_fill_rect(CENTER_X + EYE_SPACING - 25, CENTER_Y - 2, 50, 4, color);
    }
    last_drawn_height = h;
}

/* ===================== 主任务 ===================== */
void app_main(void)
{
    ESP_LOGI(TAG, "=== System starting ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_softap();
    st7789_init();
    gfx_fill_screen(GFX_BLACK);
    
    audio_player_init();

    if (rc522_init() == ESP_OK) ESP_LOGI(TAG, "RC522 ready");

    bool imu_ok = false;
    if (qmi8658_init() == ESP_OK) {
        imu_ok = true;
        vTaskDelay(pdMS_TO_TICKS(50));
        float ax, ay, az;
        qmi8658_read_accel(&ax, &ay, &az);
        filtered_ay = ay;
        ESP_LOGI(TAG, "IMU ready");
    }

    last_blink_time = millis_now();

    if (sdcard_init() == ESP_OK) {
        sd_mutex = xSemaphoreCreateMutex();

        uint8_t *v_buf = heap_caps_malloc(STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
        StaticStreamBuffer_t *v_struct = heap_caps_malloc(sizeof(StaticStreamBuffer_t), MALLOC_CAP_SPIRAM);
        video_stream_buf = xStreamBufferCreateStatic(STREAM_BUF_SIZE, 1, v_buf, v_struct);

        uint8_t *a_buf = heap_caps_malloc(STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
        StaticStreamBuffer_t *a_struct = heap_caps_malloc(sizeof(StaticStreamBuffer_t), MALLOC_CAP_SPIRAM);
        audio_stream_buf = xStreamBufferCreateStatic(STREAM_BUF_SIZE, 1, a_buf, a_struct);

        xTaskCreatePinnedToCore(sd_read_task, "sd_read", 8192, NULL, 5, NULL, 1);
        xTaskCreatePinnedToCore(tcp_server_task, "tcp", 10240, NULL, 4, NULL, 1);

        mjpeg_player_init(); 
        play_random_video(1, 5); 
    }

    xTaskCreatePinnedToCore(rc522_task, "rc522", 4096, NULL, 3, NULL, 1);
    ESP_LOGI(TAG, "=== System ready ===");

    uint32_t last_imu_time = 0;

    /* ===================== 主循环 (Core 0) ===================== */
    while (1) {
        uint32_t now = millis_now();
        float total_accel = 0;

        if (imu_ok && (now - last_imu_time >= 20)) {
            last_imu_time = now;
            float ax = 0, raw_ay = 0, az = 0;
            qmi8658_read_accel(&ax, &raw_ay, &az);
            filtered_ay = (FILTER_ALPHA * raw_ay) + ((1.0f - FILTER_ALPHA) * filtered_ay);
            total_accel = sqrtf(ax * ax + raw_ay * raw_ay + az * az);

            if (is_downloading) {
                current_state = STATE_DOWNLOADING;
            } else {
                if (current_state == STATE_DOWNLOADING) {
                    current_state = STATE_NORMAL;
                    play_random_video(1, 5);
                }

                if (total_accel > SHAKE_ACCEL_THRESHOLD) {
                    is_shaken = true; 
                    shake_start_time = now; 
                    invert_timer = 0; 
                    if (current_state != STATE_SHAKING) {
                        current_state = STATE_SHAKING;
                        play_random_video(6, 10); 
                    }
                } 
                else if (is_shaken && (now - shake_start_time > SHAKE_DURATION_MS)) {
                    is_shaken = false; 
                }

                if (!is_shaken) {
                    if (filtered_ay < INVERT_TRIGGER_AY) {
                        if (invert_timer == 0) invert_timer = now;
                        else if (now - invert_timer > INVERT_HOLD_MS && current_state != STATE_INVERTED_X) {
                            current_state = STATE_INVERTED_X;
                            play_random_video(11, 13); 
                        }
                    } 
                    else if (filtered_ay > INVERT_RELEASE_AY) {
                        invert_timer = 0;
                        if (current_state == STATE_INVERTED_X || current_state == STATE_SHAKING) {
                            current_state = STATE_NORMAL;
                            play_random_video(1, 5); 
                        }
                    }
                }
            }
        } 

        if (current_state != last_state) {
            if (current_state == STATE_DOWNLOADING) gfx_fill_screen(GFX_BLACK);
            last_drawn_height = -1; static_frame_drawn = false; last_state = current_state;
        }

        if (current_state == STATE_DOWNLOADING) {
            if (!static_frame_drawn) { draw_downloading(); static_frame_drawn = true; }
        } else {
            if (video_ready) {
                mjpeg_player_play_frame();
            } else {
                if (blink_direction == 0) {
                    if (now - last_blink_time > (uint32_t)(BLINK_MIN_INTERVAL_MS + random_range(BLINK_RANDOM_MS))) blink_direction = -1;
                    if (current_eye_height != MAX_EYE_HEIGHT) { draw_eyes_smooth(MAX_EYE_HEIGHT); current_eye_height = MAX_EYE_HEIGHT; }
                } else {
                    current_eye_height += (blink_direction * BLINK_SPEED);
                    if (current_eye_height <= 2) { current_eye_height = 2; blink_direction = 1; }
                    else if (current_eye_height >= MAX_EYE_HEIGHT) { current_eye_height = MAX_EYE_HEIGHT; blink_direction = 0; last_blink_time = now; }
                    draw_eyes_smooth(current_eye_height);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}