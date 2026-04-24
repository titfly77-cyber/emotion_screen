# ESP32 Smart Interactive Terminal (智能交互硬件终端)

[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-orange.svg)](https://www.espressif.com/)
[![Framework](https://img.shields.io/badge/Framework-ESP--IDF%20v5.1+-blue.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

本项目是一个基于 ESP32-S3 和 FreeRTOS 构建的智能交互终端底层固件。系统集成了多媒体流解码（视频+音频）、六轴姿态识别、RFID 硬件级射频交互以及局域网文件热更新功能。

该固件可作为**智能桌面陪伴机器人**的交互核心，或作为**剧本杀/桌游智能音效盒**（防止场外作弊、增强沉浸感）的软硬件一体化解决方案。

---

## ✨ 核心特性 (Key Features)

* **🎬 高性能多媒体引擎**
  * **视频系统**：基于 `StreamBuffer` 实现 SD 卡高速预读，支持 MJPEG 视频流硬解码并输出至 ST7789 屏幕（240x240），支持流畅换帧与动效 UI 系统（如随机眨眼过渡）。
  * **音频系统**：基于 I2S 的独立音频播放，支持 WAV 格式。内置软音量衰减与防破音处理，智能管控 I2S 时钟休眠以彻底消除待机底噪。
* **🧭 物理姿态交互 (IMU)**
  * 集成 QMI8658 六轴传感器，通过卡尔曼/低通滤波进行轻量级姿态解算。
  * 精准识别 **“摇晃 (Shake)”** 和 **“倒置 (Invert)”** 等物理动作，并动态联动屏幕触发专属多媒体反馈。
* **💳 零误触 RFID 射频交互**
  * 深度定制的 RC522 射频驱动，摒弃传统的纯软件轮询，采用 **“物理 IRQ 中断 (下降沿) + SPI 寄存器防冲突”** 的严格双重校验读取机制。
  * 卡片 UID 映射绑定：支持刷入特定实体卡片无缝切歌或触发指定剧情音效。
* **🛜 局域网热更新 (OTA Media)**
  * 设备自带 Wi-Fi SoftAP 热点 (`waveshare_esp32`)。
  * 内置 TCP Socket 服务器 (Port: 3333)，支持通过局域网直接向设备的 SD 卡下发、覆盖媒体素材，实现免拆卸的内容迭代。

---

## ⚙️ 硬件引脚映射 (Hardware Pinout)

系统采用 SPI、I2C、I2S、SDMMC 多总线并行架构，引脚分配如下：

| 模块名称 | 核心驱动 | 引脚映射 (GPIO) | 总线/接口说明 |
| :--- | :--- | :--- | :--- |
| **显示屏** | ST7789 (240x240) | MOSI:41, SCLK:40, CS:39, DC:38, RST:42 | SPI2，支持 DMA 传输加速 |
| **IMU 传感器** | QMI8658 | SDA:47, SCL:48 | I2C0，1000Hz 加速度采样 |
| **射频读卡器** | RC522 | MOSI:11, MISO:13, SCK:12, CS:10, RST:9, IRQ:8 | SPI3，独立硬件 IRQ 中断使能 |
| **存储卡** | SD Card | D0:16, D3:17, CMD:18, CLK:21 | SDMMC 1-Bit 模式，高速 FATFS |
| **音频输出** | I2S DAC / Amp | BCK:3, WS:4, DO:5 | I2S Std 模式，输出至外接功放 |

---

## 🏗️ 系统架构 (Software Architecture)

系统完全基于 **FreeRTOS** 开发，采用多任务解耦设计，确保高负载下的实时交互响应：

1. **`app_main` (Core 0)**: 状态机主控逻辑。负责 IMU 轮询滤波、状态切换（Normal / Inverted / Shaking / Downloading）及原生 GFX 动效绘制。
2. **`sd_read_task`**: 并发文件 IO 任务。基于互斥锁 (`sd_mutex`) 安全地向音/视频 `StreamBuffer` 预填充数据流。
3. **`audio_play_task`**: 临时创建的后台 I2S 吐流任务，播放完毕自动销毁并关闭时钟。
4. **`rc522_task`**: 射频事件监听守护进程，阻塞等待硬件信号量，命中绑定的 UID 后下发播放指令。
5. **`tcp_server_task`**: 常驻 TCP 守护进程，用于接收媒体文件写入 SD 卡。

---

## 🚀 快速开始 (Getting Started)

### 1. 环境依赖
* 开发框架：[ESP-IDF v5.1+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
* 依赖组件包 (已在 `idf_component.yml` 中声明)：
  * `espressif/esp_jpeg` (JPEG 硬件解码)
  * `abobija/rc522` (RFID 驱动基础库)

### 2. 准备媒体文件
请确保 SD 卡已格式化为 FAT32，并在根目录放入以下测试文件：
* **视频素材 (240x240 MJPEG)**：`/v1.mjpeg` 到 `/v13.mjpeg`
  * `v1~v5`: 待机随机播放
  * `v6~v10`: 摇晃触发播放
  * `v11~v13`: 倒置触发播放
* **音频素材 (WAV格式)**：`/s1.wav` 到 `/s4.wav` (分别映射四张 RFID 卡片)

### 3. 配置与编译
拉取代码后，如需修改绑定的卡片，请在 `main.c` 中替换实际的卡片 UID：
```c
#define UID_CARD_1 {0x63, 0x18, 0x29, 0x07} // 替换为你自己的卡片 UID
