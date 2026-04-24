#include "rc522.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static spi_device_handle_t spi_dev;
static const char *TAG = "RC522_STRICT";

/* 硬件中断信号量 */
static SemaphoreHandle_t rc522_irq_sem = NULL;

/* RC522 寄存器地址 */
#define CommandReg      0x01
#define ComIEnReg       0x02
#define DivIEnReg       0x03
#define CommIrqReg      0x04
#define ErrorReg        0x06
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define ControlReg      0x0C
#define BitFramingReg   0x0D
#define ModeReg         0x11
#define TxControlReg    0x14
#define TxAutoReg       0x15
#define RxGainReg       0x26
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D

#define PCD_IDLE        0x00
#define PCD_TRANSCEIVE  0x0C
#define PCD_RESETPHASE  0x0F

/* ---------------- 中断服务函数 (ISR) ---------------- */
static void IRAM_ATTR rc522_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(rc522_irq_sem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/* ---------------- 基础 SPI 读写 ---------------- */
static void rc522_write_reg(uint8_t addr, uint8_t val) {
    uint8_t tx_data[2] = { (addr << 1) & 0x7E, val };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx_data };
    spi_device_transmit(spi_dev, &t);
}

static uint8_t rc522_read_reg(uint8_t addr) {
    uint8_t tx_data[2] = { ((addr << 1) & 0x7E) | 0x80, 0x00 };
    uint8_t rx_data[2] = { 0 };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx_data, .rx_buffer = rx_data };
    spi_device_transmit(spi_dev, &t);
    return rx_data[1];
}

static void rc522_set_bit_mask(uint8_t reg, uint8_t mask) {
    rc522_write_reg(reg, rc522_read_reg(reg) | mask);
}

static void rc522_clear_bit_mask(uint8_t reg, uint8_t mask) {
    rc522_write_reg(reg, rc522_read_reg(reg) & (~mask));
}

/* ---------------- 初始化 ---------------- */
esp_err_t rc522_init(void) {
    /* 1. 初始化 SPI 总线 */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = RC522_PIN_MOSI,
        .miso_io_num = RC522_PIN_MISO,
        .sclk_io_num = RC522_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 5 * 1000 * 1000, 
        .mode = 0,
        .spics_io_num = RC522_PIN_CS, 
        .queue_size = 7,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev_cfg, &spi_dev));

    /* 2. 初始化复位引脚 */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << RC522_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);
    
    /* 3. 初始化中断 (IRQ) 引脚 */
    rc522_irq_sem = xSemaphoreCreateBinary();
    gpio_config_t irq_cfg = {
        .pin_bit_mask = (1ULL << RC522_PIN_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE, // 下降沿触发
    };
    gpio_config(&irq_cfg);
    
    esp_err_t isr_ret = gpio_install_isr_service(0);
    if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service");
    }
    gpio_isr_handler_add(RC522_PIN_IRQ, rc522_isr_handler, NULL);

    /* 4. 硬件复位 */
    gpio_set_level(RC522_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RC522_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    rc522_write_reg(CommandReg, PCD_RESETPHASE);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 5. 核心射频配置 */
    rc522_write_reg(TModeReg, 0x8D);
    rc522_write_reg(TPrescalerReg, 0x3E);
    rc522_write_reg(TReloadRegL, 30);
    rc522_write_reg(TReloadRegH, 0);

    /* ★ 核心强化 ★ */
    rc522_write_reg(TxAutoReg, 0x40);    // 强制 100% ASK 调制，适配手机
    rc522_write_reg(RxGainReg, 0x70);    // 接收灵敏度拉满 48dB
    rc522_write_reg(DivIEnReg, 0x80);    // 强制 IRQ 引脚为推挽输出(Push-Pull)，彻底解决电平下不来的问题
    rc522_write_reg(TxControlReg, 0x83); // 开启天线

    ESP_LOGI(TAG, "RC522 Strict Mode Ready!");
    return ESP_OK;
}

/* ---------------- 严格校验双重读取机制 ---------------- */
static esp_err_t rc522_to_card_strict(uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint32_t *backLen) {
    esp_err_t status = ESP_FAIL;
    uint8_t irqEn = 0x77; 

    // 设置 IRqInv=1(电平反转以适配下降沿)
    rc522_write_reg(ComIEnReg, irqEn | 0x80); 
    rc522_write_reg(CommIrqReg, 0x7F); // 清除旧的中断标志
    rc522_set_bit_mask(FIFOLevelReg, 0x80); // 清空 FIFO

    rc522_write_reg(CommandReg, PCD_IDLE);

    // 填入发射数据
    for (int i = 0; i < sendLen; i++) {
        rc522_write_reg(FIFODataReg, sendData[i]);
    }

    // 清空软件残留信号
    xSemaphoreTake(rc522_irq_sem, 0);

    rc522_write_reg(CommandReg, command);
    if (command == PCD_TRANSCEIVE) {
        rc522_set_bit_mask(BitFramingReg, 0x80); // 开始发射
    }

    /* * 双重保险机制：
     * 1. 优先等待物理 IRQ 线的硬件中断（极速响应）
     * 2. 如果 35 毫秒还没来，继续往下走，用软件直接读寄存器（兜底防断线）
     */
    bool irq_triggered = (xSemaphoreTake(rc522_irq_sem, pdMS_TO_TICKS(35)) == pdTRUE);
    
    // 不管是硬件触发还是超时，一律严格检查寄存器真伪
    uint8_t n = rc522_read_reg(CommIrqReg);
    rc522_clear_bit_mask(BitFramingReg, 0x80);

    // ★ 如果是超时定时器中断 (0x01)，代表绝绝对对没有卡片，一票否决！
    if (n & 0x01) {
        return ESP_FAIL; 
    }

    // ★ 必须且只能是收到了物理回波 (RxIRq = 0x20)，才允许解析！
    if (n & 0x20) { 
        if (!(rc522_read_reg(ErrorReg) & 0x1B)) { // 无底层协议错误
            status = ESP_OK;
            if (command == PCD_TRANSCEIVE) {
                uint8_t len = rc522_read_reg(FIFOLevelReg);
                uint8_t lastBits = rc522_read_reg(ControlReg) & 0x07;
                if (lastBits) *backLen = (len - 1) * 8 + lastBits;
                else *backLen = len * 8;
                
                if (len == 0) len = 1;
                if (len > 16) len = 16;
                for (int j = 0; j < len; j++) backData[j] = rc522_read_reg(FIFODataReg);
            }
        }
    }

    return status;
}

bool rc522_read_uid(uint8_t *uid) {
    uint8_t req_buf[2] = {0x26}; // 寻卡
    uint8_t back_data[16];
    uint32_t back_len;

    rc522_write_reg(BitFramingReg, 0x07);
    
    // 第 1 关：发送短波寻找附近有没有卡
    if (rc522_to_card_strict(PCD_TRANSCEIVE, req_buf, 1, back_data, &back_len) == ESP_OK) {
        // 第 2 关：确认有卡后，防冲突读取真实 UID
        uint8_t anticoll_buf[2] = {0x93, 0x20}; 
        rc522_write_reg(BitFramingReg, 0x00);
        if (rc522_to_card_strict(PCD_TRANSCEIVE, anticoll_buf, 2, back_data, &back_len) == ESP_OK) {
            memcpy(uid, back_data, 4);
            return true;
        }
    }
    return false;
}