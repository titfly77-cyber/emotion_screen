#pragma once

#include "esp_err.h"
#include <stdio.h>

/* ===================== SD_MMC 引脚定义 ===================== */
#define SD_MMC_D3_PIN   17
#define SD_MMC_CMD_PIN  18
#define SD_MMC_CLK_PIN  21
#define SD_MMC_D0_PIN   16

/* 挂载点 */
#define SD_MOUNT_POINT  "/sdcard"

/* ===================== API ===================== */

/**
 * @brief 初始化 SD_MMC (1-bit 模式)
 * @return ESP_OK 成功, 其他为失败
 */
esp_err_t sdcard_init(void);

/**
 * @brief 卸载 SD 卡
 */
void sdcard_deinit(void);

/**
 * @brief 打开文件
 */
FILE *sdcard_fopen(const char *path, const char *mode);