#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* 根据你的要求配置引脚 */
#define RC522_PIN_MOSI  11
#define RC522_PIN_MISO  13
#define RC522_PIN_SCK   12
#define RC522_PIN_CS    10
#define RC522_PIN_RST   9
#define RC522_PIN_IRQ   8

/**
 * @brief 初始化 RC522 模块 (使用 SPI3_HOST)
 */
esp_err_t rc522_init(void);

/**
 * @brief 检测是否有一张卡片在附近并读取其 UID
 * @param uid 长度必须至少为 4 字节的数组
 * @return true: 读卡成功, false: 未找到卡片
 */
bool rc522_read_uid(uint8_t *uid);