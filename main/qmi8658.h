#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* ===================== I2C 引脚 (与 Arduino 一致) ===================== */
#define QMI8658_I2C_SDA     47
#define QMI8658_I2C_SCL     48
#define QMI8658_I2C_ADDR    0x6B
#define QMI8658_I2C_FREQ_HZ 400000

/* ... 其余内容不变 ... */
/**
 * @brief 检查是否有新数据可读
 */
 bool qmi8658_is_available(void);


/* ===================== 寄存器定义 ===================== */
#define QMI8658_REG_WHO_AM_I    0x00
#define QMI8658_REG_REVISION    0x01
#define QMI8658_REG_CTRL1       0x02
#define QMI8658_REG_CTRL2       0x03   /* 加速度计配置 */
#define QMI8658_REG_CTRL3       0x04   /* 陀螺仪配置 */
#define QMI8658_REG_CTRL5       0x06   /* 低通滤波器配置 */
#define QMI8658_REG_CTRL7       0x08   /* 传感器使能 */
#define QMI8658_REG_STATUSINT   0x2D
#define QMI8658_REG_STATUS0     0x2E
#define QMI8658_REG_TEMP_L      0x33
#define QMI8658_REG_AX_L        0x35   /* 加速度数据起始 */
#define QMI8658_REG_GX_L        0x3B   /* 陀螺仪数据起始 */
#define QMI8658_REG_RESET       0x60

/* ===================== API ===================== */

/**
 * @brief 初始化 QMI8658 (与 Arduino WS_QMI8658 配置完全一致)
 *        - 加速度: ±4g, ODR 1000Hz, LPF Mode 0
 *        - 陀螺仪: ±64dps, ODR 896.8Hz, LPF Mode 3
 */
esp_err_t qmi8658_init(void);

/**
 * @brief 读取加速度 (返回值 = 原始g值 × 10.0, 与 Arduino QMI8658_get_A_fx/fy/fz 一致)
 */
void qmi8658_read_accel(float *ax, float *ay, float *az);

/**
 * @brief 读取陀螺仪 (dps)
 */
void qmi8658_read_gyro(float *gx, float *gy, float *gz);

