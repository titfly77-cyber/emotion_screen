#include "sdcard.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"

static const char *TAG = "SDCARD";
static sdmmc_card_t *card = NULL;

esp_err_t sdcard_init(void)
{
    /* 先拉高 D3 引脚 (芯片选择) */
    gpio_config_t d3_cfg = {
        .pin_bit_mask = (1ULL << SD_MMC_D3_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&d3_cfg);
    gpio_set_level(SD_MMC_D3_PIN, 1);

    ESP_LOGI(TAG, "初始化 SD_MMC (1-bit mode)...");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = 8000;  /* 与 Arduino 一致 8MHz */
    host.flags = SDMMC_HOST_FLAG_1BIT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk   = (gpio_num_t)SD_MMC_CLK_PIN;
    slot.cmd   = (gpio_num_t)SD_MMC_CMD_PIN;
    slot.d0    = (gpio_num_t)SD_MMC_D0_PIN;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD卡挂载失败: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD卡挂载成功: %s", SD_MOUNT_POINT);
    return ESP_OK;
}

void sdcard_deinit(void)
{
    if (card) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        card = NULL;
        ESP_LOGI(TAG, "SD卡已卸载");
    }
}

FILE *sdcard_fopen(const char *path, const char *mode)
{
    /* path 例如 "/v1.mjpeg", 需要拼接挂载点 */
    char fullpath[128];
    if (path[0] == '/') {
        snprintf(fullpath, sizeof(fullpath), "%s%s", SD_MOUNT_POINT, path);
    } else {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", SD_MOUNT_POINT, path);
    }
    FILE *f = fopen(fullpath, mode);
    if (!f) {
        ESP_LOGE("SDCARD", "无法打开文件: %s", fullpath);
    }
    return f;
}