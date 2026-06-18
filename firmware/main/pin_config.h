/*
 * pin_config.h — Ink-Player 引脚定义
 * ESP32-PICO-V3-02, 4 层板, 2026-06
 */

#pragma once

/* ── 墨水屏 SPI (SSD1677) ── */
#define EPD_MOSI     13
#define EPD_CLK      14
#define EPD_CS        5
#define EPD_DC       19
#define EPD_RST      22
#define EPD_BUSY     33

/* ── I2S 音频 (INMP441 + MAX98357A) ── */
#define I2S_BCLK     26
#define I2S_WS       25
#define I2S_DIN      27   // INMP441 SD
#define I2S_DOUT     21   // MAX98357A DIN
#define I2S_MCLK     -1   // not used

/* ── MAX98357A 控制 ── */
#define AMP_SD       4    // SD_MODE: high=on

/* ── 旋转编码器 ── */
#define ENC_A        35   // CW=next
#define ENC_B        36   // CCW=confirm

/* ── ADC 电池检测 ── */
#define ADC_BAT      34   // VBAT/2 via 100k+100k divider

/* ── I2C (预留) ── */
#define I2C_SDA      15
#define I2C_SCL      32

/* ── 墨水屏规格 ── */
#define EPD_WIDTH    800
#define EPD_HEIGHT   480
#define EPD_BUF_SIZE (EPD_WIDTH * EPD_HEIGHT / 8)  // 48000 bytes

/* ── 状态栏 ── */
#define STATUSBAR_H   40   // 顶部 40px 状态栏
#define FONT_W         8
#define FONT_H        16
