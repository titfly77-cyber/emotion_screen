# ESP32 Smart Interactive Terminal

[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-orange.svg)](https://www.espressif.com/)
[![Framework](https://img.shields.io/badge/Framework-ESP--IDF%20v5.1+-blue.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

This project is a low-level firmware for a smart interactive terminal built on ESP32-S3 and FreeRTOS. The system integrates multimedia stream decoding (video + audio), 6-axis posture recognition, hardware-level RFID interaction, and LAN-based file hot-updating capabilities.

This firmware can serve as the interactive core for a **smart desktop companion robot**, or as an integrated hardware-software solution for a **smart soundbox for LARP/board games** (to prevent out-of-game cheating and enhance immersion).

---

## ✨ Key Features

* **🎬 High-Performance Multimedia Engine**
  * **Video System**: Utilizes `StreamBuffer` for high-speed SD card pre-reading, supporting hardware decoding of MJPEG video streams outputting to an ST7789 display (240x240). Features smooth frame switching and an animated UI system (e.g., random blinking transitions).
  * **Audio System**: Independent audio playback based on I2S, supporting WAV format. Includes built-in software volume attenuation and anti-clipping processing, with intelligent I2S clock sleep management to completely eliminate standby noise.
* **🧭 Physical Posture Interaction (IMU)**
  * Integrates the QMI8658 6-axis sensor, performing lightweight posture calculation via Kalman/low-pass filtering.
  * Accurately recognizes physical actions such as **"Shake"** and **"Invert"**, dynamically linking with the screen to trigger exclusive multimedia feedback.
* **💳 Zero-False-Touch RFID Interaction**
  * Deeply customized RC522 RF driver, abandoning traditional pure software polling in favor of a strict dual-verification reading mechanism using **"Physical IRQ Interrupts (falling edge) + SPI Register Anti-collision"**.
  * Card UID Mapping: Supports swiping specific physical cards to seamlessly switch tracks or trigger designated storyline sound effects.
* **🛜 LAN Hot Updates (OTA Media)**
  * The device features a built-in Wi-Fi SoftAP hotspot (`waveshare_esp32`).
  * Built-in TCP Socket server (Port: 3333) enables direct downloading and overwriting of media assets to the device's SD card over the LAN, allowing for content iteration without disassembly.

---

## ⚙️ Hardware Pinout

The system utilizes a parallel architecture of multiple buses including SPI, I2C, I2S, and SDMMC. The pin assignments are as follows:

| Module | Core Driver | Pin Mapping (GPIO) | Bus/Interface Description |
| :--- | :--- | :--- | :--- |
| **Display** | ST7789 (240x240) | MOSI:41, SCLK:40, CS:39, DC:38, RST:42 | SPI2, supports DMA transfer acceleration |
| **IMU Sensor** | QMI8658 | SDA:47, SCL:48 | I2C0, 1000Hz acceleration sampling |
| **RFID Reader** | RC522 | MOSI:11, MISO:13, SCK:12, CS:10, RST:9, IRQ:8 | SPI3, independent hardware IRQ interrupt enabled |
| **Storage Card** | SD Card | D0:16, D3:17, CMD:18, CLK:21 | SDMMC 1-Bit mode, high-speed FATFS |
| **Audio Output** | I2S DAC / Amp | BCK:3, WS:4, DO:5 | I2S Std mode, output to external amplifier |

---

## 🏗️ Software Architecture

The system is developed entirely on **FreeRTOS**, utilizing a multi-task decoupling design to ensure real-time interactive responses under high loads:

1. **`app_main` (Core 0)**: State machine master control logic. Responsible for IMU polling filtering, state switching (Normal / Inverted / Shaking / Downloading), and native GFX animation rendering.
2. **`sd_read_task`**: Concurrent file I/O task. Safely pre-fills audio/video `StreamBuffer` data streams based on a mutex (`sd_mutex`).
3. **`audio_play_task`**: A temporary background I2S streaming task that automatically destroys itself and shuts off the clock upon playback completion.
4. **`rc522_task`**: RF event listener daemon. Blocks while waiting for a hardware semaphore and issues playback commands upon matching a bound UID.
5. **`tcp_server_task`**: Resident TCP daemon used to receive media files and write them to the SD card.

---

## 🚀 Getting Started

### 1. Environmental Dependencies
* Development Framework: [ESP-IDF v5.1+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
* Dependency Components (declared in `idf_component.yml`):
  * `espressif/esp_jpeg` (JPEG hardware decoding)
  * `abobija/rc522` (Base library for RFID driver)

### 2. Prepare Media Files
Please ensure the SD card is formatted to FAT32, and place the following test files in the root directory:
* **Video Assets (240x240 MJPEG)**: `/v1.mjpeg` to `/v13.mjpeg`
  * `v1~v5`: Random standby playback
  * `v6~v10`: Playback triggered by shaking
  * `v11~v13`: Playback triggered by inversion
* **Audio Assets (WAV format)**: `/s1.wav` to `/s4.wav` (mapped to four RFID cards respectively)

### 3. Configuration and Compilation
After pulling the code, if you need to modify the bound cards, please replace the actual card UIDs in `main.c`:
```c
#define UID_CARD_1 {0x63, 0x18, 0x29, 0x07} // Replace with your own card UID
