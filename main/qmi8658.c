#include "qmi8658.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "QMI8658";

/* 新版 I2C 句柄 */
static i2c_master_bus_handle_t  bus_handle = NULL;
static i2c_master_dev_handle_t  dev_handle = NULL;

static const float ACC_SCALE  = 4.0f / 32768.0f * 10.0f;
static const float GYRO_SCALE = 64.0f / 32768.0f;

/* ===================== I2C 新API 读写 ===================== */
static esp_err_t qmi_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev_handle, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t qmi_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev_handle, &reg, 1, data, len,
                                        pdMS_TO_TICKS(100));
}

static uint8_t qmi_read_reg(uint8_t reg)
{
    uint8_t val = 0;
    qmi_read_regs(reg, &val, 1);
    return val;
}

static bool qmi_data_ready(void)
{
    uint8_t status = qmi_read_reg(QMI8658_REG_STATUSINT);
    return (status & 0x01) != 0;
}

/* ===================== 添加I2C设备 ===================== */
static esp_err_t add_i2c_device(uint8_t addr, i2c_master_dev_handle_t *out_dev)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = QMI8658_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(bus_handle, &dev_cfg, out_dev);
}

/* ===================== I2C 扫描 ===================== */
static uint8_t i2c_scan_for_qmi8658(void)
{
    uint8_t candidates[] = {0x6A, 0x6B};

    for (int i = 0; i < 2; i++) {
        uint8_t addr = candidates[i];
        i2c_master_dev_handle_t tmp_dev = NULL;

        if (add_i2c_device(addr, &tmp_dev) != ESP_OK) continue;

        uint8_t reg = 0x00;
        uint8_t val = 0;
        esp_err_t ret = i2c_master_transmit_receive(tmp_dev, &reg, 1, &val, 1,
                                                     pdMS_TO_TICKS(50));

        ESP_LOGI(TAG, "Probe 0x%02X: ret=%s, WHO_AM_I=0x%02X",
                 addr, esp_err_to_name(ret), val);

        if (ret == ESP_OK && val == 0x05) {
            dev_handle = tmp_dev;
            return addr;
        }

        i2c_master_bus_rm_device(tmp_dev);
    }

    return 0;
}

/* ===================== 初始化 ===================== */
esp_err_t qmi8658_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port   = I2C_NUM_0,
        .sda_io_num = QMI8658_I2C_SDA,
        .scl_io_num = QMI8658_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t found_addr = i2c_scan_for_qmi8658();
    if (found_addr == 0) {
        ESP_LOGE(TAG, "QMI8658 not found! (SDA=%d, SCL=%d)",
                 QMI8658_I2C_SDA, QMI8658_I2C_SCL);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "QMI8658 found at 0x%02X", found_addr);

    uint8_t rev = qmi_read_reg(QMI8658_REG_REVISION);
    ESP_LOGI(TAG, "Revision: 0x%02X", rev);

    /* 软件复位 */
    qmi_write_reg(QMI8658_REG_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(30));
    for (int i = 0; i < 50; i++) {
        if (qmi_read_reg(QMI8658_REG_RESET) == 0x00) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_LOGI(TAG, "WHO_AM_I after reset: 0x%02X", qmi_read_reg(QMI8658_REG_WHO_AM_I));

    qmi_write_reg(QMI8658_REG_CTRL1, 0x40);   /* 地址自增 */
    qmi_write_reg(QMI8658_REG_CTRL2, 0x13);   /* ±4g, 1000Hz */
    qmi_write_reg(QMI8658_REG_CTRL3, 0x23);   /* ±64dps, 896.8Hz */
    qmi_write_reg(QMI8658_REG_CTRL5, 0x71);   /* LPF: Accel Mode0, Gyro Mode3 */
    qmi_write_reg(QMI8658_REG_CTRL7, 0x03);   /* 使能 Accel + Gyro */

    vTaskDelay(pdMS_TO_TICKS(30));

    ESP_LOGI(TAG, "CTRL1=0x%02X CTRL2=0x%02X CTRL3=0x%02X CTRL5=0x%02X CTRL7=0x%02X",
             qmi_read_reg(QMI8658_REG_CTRL1),
             qmi_read_reg(QMI8658_REG_CTRL2),
             qmi_read_reg(QMI8658_REG_CTRL3),
             qmi_read_reg(QMI8658_REG_CTRL5),
             qmi_read_reg(QMI8658_REG_CTRL7));

    ESP_LOGI(TAG, "Init done (±4g 1000Hz, ±64dps 896.8Hz)");
    return ESP_OK;
}

/* ===================== 读取加速度 ===================== */
void qmi8658_read_accel(float *ax, float *ay, float *az)
{
    for (int i = 0; i < 5; i++) {
        if (qmi_data_ready()) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint8_t buf[6];
    qmi_read_regs(QMI8658_REG_AX_L, buf, 6);

    int16_t raw_ax = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t raw_ay = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t raw_az = (int16_t)((buf[5] << 8) | buf[4]);

    *ax = raw_ax * ACC_SCALE;
    *ay = raw_ay * ACC_SCALE;
    *az = raw_az * ACC_SCALE;
}

/* ===================== 读取陀螺仪 ===================== */
void qmi8658_read_gyro(float *gx, float *gy, float *gz)
{
    uint8_t buf[6];
    qmi_read_regs(QMI8658_REG_GX_L, buf, 6);

    int16_t raw_gx = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t raw_gy = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t raw_gz = (int16_t)((buf[5] << 8) | buf[4]);

    *gx = raw_gx * GYRO_SCALE;
    *gy = raw_gy * GYRO_SCALE;
    *gz = raw_gz * GYRO_SCALE;
}

/* ===================== 数据检查 ===================== */
bool qmi8658_is_available(void)
{
    return qmi_data_ready();
}