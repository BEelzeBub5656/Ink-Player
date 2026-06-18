/*
 * display.c — SSD1677 墨水屏驱动 (SPI)
 *
 * 面板: HINK-E075A07-A0 7.5" 三色
 * 分辨率: 880×528 (BW + Red)
 * 控制器: SSD1677
 *
 * 参考:
 *   - SSD1677 datasheet (Waveshare)
 *   - papyrix-reader SSD1677 guide (GitHub)
 *   - GxEPD2 library init sequence
 *
 * BUSY 极性: HIGH=忙碌, LOW=空闲
 * 全刷: ~15s
 * 局刷: ~2-4s
 */

#include "display.h"
#include "pin_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

static spi_device_handle_t spi;
static uint8_t *bw_buf  = NULL;  // 58080 bytes (DMA-capable DRAM)
static uint8_t *red_buf = NULL;
static int partial_count = 0;

/* ── SSD1677 命令 ── */
#define CMD_PANEL_SETTING     0x00
#define CMD_DRIVER_OUTPUT     0x01
#define CMD_POWER_OFF         0x02
#define CMD_POWER_ON          0x04
#define CMD_DEEP_SLEEP        0x07   // + 0xA5 check code
#define CMD_BOOSTER_SOFTSTART 0x0C
#define CMD_DATA_ENTRY        0x11
#define CMD_SW_RESET          0x12
#define CMD_TEMP_SENSOR       0x18
#define CMD_MASTER_ACTIVATE   0x20
#define CMD_DISP_UPDATE_CTRL1 0x21
#define CMD_DISP_UPDATE_CTRL2 0x22
#define CMD_WRITE_BW          0x24
#define CMD_WRITE_RED         0x26
#define CMD_VCOM              0x2C
#define CMD_LOAD_LUT          0x32
#define CMD_BORDER            0x3C
#define CMD_SET_RAMX          0x44
#define CMD_SET_RAMY          0x45
#define CMD_VCOM_DATA_INTERVAL 0x50

/* ── 辅助宏 ── */
#define spi_cmd(cmd)       _spi_send_cmd(cmd)
#define spi_data(d)        _spi_send_data(d)
#define spi_data_buf(p, n) _spi_send_buffer(p, n)

static void _spi_send_cmd(uint8_t cmd) {
    gpio_set_level(EPD_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_transmit(spi, &t);
}

static void _spi_send_data(uint8_t data) {
    gpio_set_level(EPD_DC, 1);
    spi_transaction_t t = { .length = 8, .tx_buffer = &data };
    spi_device_transmit(spi, &t);
}

static void _spi_send_buffer(const uint8_t *buf, size_t len) {
    gpio_set_level(EPD_DC, 1);
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = buf,
    };
    spi_device_transmit(spi, &t);
}

/* ── BUSY 等待: HIGH=忙碌 ── */
static void _wait_busy(int timeout_ms) {
    int waited = 0;
    while (gpio_get_level(EPD_BUSY) == 1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
        if (timeout_ms && waited > timeout_ms) {
            ESP_LOGW(TAG, "BUSY timeout after %d ms", waited);
            break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
   LUT 波形表 (SSD1677 完整 111 字节)
   来源: papyrix-reader SSD1677 reference
   ═══════════════════════════════════════════════════════════════ */
static const uint8_t lut_full[] = {
    /* Phase 0 (15 bytes × 7 groups = 105 bytes) */
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99,
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99,
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99,
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99,
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99,
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99,
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99,
    /* Phase 1 (1 reserved byte) */
    0x00,
    /* VCOM / voltage (5 bytes) */
    0x22, 0x17, 0x41, 0x00, 0x32,
};

/* ── SPI 总线初始化 ── */
static void _spi_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = EPD_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = EPD_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = EPD_BUF_SIZE,  // 58080
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 20 * 1000 * 1000,  // 20 MHz
        .mode           = 0,
        .spics_io_num   = EPD_CS,
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi));
}

/* ── GPIO 初始化 ── */
static void _gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << EPD_DC) | (1ULL << EPD_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(EPD_DC, 0);
    gpio_set_level(EPD_CS, 1);

    /* BUSY = 输入, 上拉 */
    gpio_set_direction(EPD_BUSY, GPIO_MODE_INPUT);
    gpio_set_pull_mode(EPD_BUSY, GPIO_PULLUP_ONLY);
}

/* ── 硬件复位 ── */
static void _hw_reset(void)
{
    gpio_set_level(EPD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    _wait_busy(200);
}

/* ═══════════════════════════════════════════════════════════════
   SSD1677 完整初始化序列 (按数据手册 §8)
   ═══════════════════════════════════════════════════════════════ */
static void _ssd1677_init(void)
{
    /* SW Reset */
    spi_cmd(CMD_SW_RESET);
    vTaskDelay(pdMS_TO_TICKS(100));
    _wait_busy(500);

    /* Panel Setting: LUT from register (not OTP), KW-BF=1, KWR=1 */
    spi_cmd(CMD_PANEL_SETTING);       // 0x00
    spi_data(0x0F);                   // LUT_REG=1, BW=1, RED=1

    /* Driver Output Control: 528 rows */
    spi_cmd(CMD_DRIVER_OUTPUT);       // 0x01
    spi_data((EPD_HEIGHT - 1) & 0xFF);       // MUX low
    spi_data(((EPD_HEIGHT - 1) >> 8) & 0xFF); // MUX high
    spi_data(0x00);                          // GD=0, SM=0, TB=0

    /* Booster Soft Start (5 bytes) */
    spi_cmd(CMD_BOOSTER_SOFTSTART);   // 0x0C
    spi_data(0xD7); spi_data(0xD6); spi_data(0x9D);

    /* VCOM Register */
    spi_cmd(CMD_VCOM);                // 0x2C
    spi_data(0xA8);

    /* Border Waveform: follow LUT0 for VSH/VSL */
    spi_cmd(CMD_BORDER);              // 0x3C
    spi_data(0x03);

    /* Temperature Sensor: internal */
    spi_cmd(CMD_TEMP_SENSOR);         // 0x18
    spi_data(0x48);                   // internal sensor, enable

    /* Data Entry Mode: X inc, Y dec */
    spi_cmd(CMD_DATA_ENTRY);          // 0x11
    spi_data(0x03);

    /* RAM X range: 0 to (880/8 - 1) = 109 */
    spi_cmd(CMD_SET_RAMX);
    spi_data(0x00);
    spi_data((EPD_WIDTH / 8 - 1) & 0xFF);

    /* RAM Y range: 0 to 527 */
    spi_cmd(CMD_SET_RAMY);
    spi_data(0x00); spi_data(0x00);
    spi_data((EPD_HEIGHT - 1) & 0xFF);
    spi_data(((EPD_HEIGHT - 1) >> 8) & 0xFF);

    /* VCOM + Data Interval */
    spi_cmd(CMD_VCOM_DATA_INTERVAL);  // 0x50
    spi_data(0x97);                   // border output

    /* Load LUT (111 bytes) */
    spi_cmd(CMD_LOAD_LUT);            // 0x32
    spi_data_buf(lut_full, sizeof(lut_full));

    ESP_LOGI(TAG, "SSD1677 880×528 init done, LUT=%d bytes", (int)sizeof(lut_full));
}

/* ═══════════════════════════════════════════════════════════════
   公开 API
   ═══════════════════════════════════════════════════════════════ */

void display_init(void)
{
    _gpio_init();
    _spi_init();

    /* 帧缓冲 (DMA 可访问 DRAM, NOT PSRAM) */
    bw_buf  = heap_caps_malloc(EPD_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    red_buf = heap_caps_malloc(EPD_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    assert(bw_buf && red_buf);
    memset(bw_buf,  0xFF, EPD_BUF_SIZE);  // 全白
    memset(red_buf, 0x00, EPD_BUF_SIZE);  // 无红

    _hw_reset();
    _ssd1677_init();

    ESP_LOGI(TAG, "Display ready: 880×528, bw=%p red=%p", bw_buf, red_buf);
}

/* ── 全屏刷新 (全刷 LUT, ~15s) ── */
void display_full_refresh(void)
{
    partial_count = 0;

    /* 写 BW 缓冲 */
    spi_cmd(CMD_WRITE_BW);
    spi_data_buf(bw_buf, EPD_BUF_SIZE);

    /* 写 RED 缓冲 */
    spi_cmd(CMD_WRITE_RED);
    spi_data_buf(red_buf, EPD_BUF_SIZE);

    /* 触发全刷 */
    spi_cmd(CMD_DISP_UPDATE_CTRL2);
    spi_data(0xC7);   // using LUT from register (0x32)
    spi_cmd(CMD_MASTER_ACTIVATE);
    _wait_busy(0);    // 无限等 BUSY 释放

    ESP_LOGD(TAG, "Full refresh done");
}

/* ── 窗口局部刷新 ── */
void display_partial_refresh(int x, int y, int w, int h)
{
    partial_count++;
    /* 每 8 次局刷后强制全刷清残影 */
    if (partial_count >= 8) {
        display_full_refresh();
        return;
    }

    /* 窗口: 按字节对齐 */
    int x_byte  = x & ~0x07;          // 下取整到 8 倍数
    int w_bytes = ((w + (x - x_byte) + 7) / 8);

    spi_cmd(CMD_SET_RAMX);
    spi_data(x_byte);
    spi_data((x_byte + w_bytes - 1) & 0xFF);

    spi_cmd(CMD_SET_RAMY);
    spi_data(y & 0xFF);
    spi_data((y >> 8) & 0xFF);
    spi_data((y + h - 1) & 0xFF);
    spi_data(((y + h - 1) >> 8) & 0xFF);

    /* 只写窗口内的 BW 数据 */
    spi_cmd(CMD_WRITE_BW);
    for (int row = y; row < y + h; row++) {
        int offset = row * (EPD_WIDTH / 8) + x_byte;
        spi_data_buf(bw_buf + offset, w_bytes);
    }

    /* 触发局刷 */
    spi_cmd(CMD_DISP_UPDATE_CTRL2);
    spi_data(0x0F);   // partial update, LUT from register
    spi_cmd(CMD_MASTER_ACTIVATE);
    _wait_busy(0);

    ESP_LOGD(TAG, "Partial (%d,%d %dx%d) #%d", x, y, w, h, partial_count);
}

/* ── 缓冲访问 ── */
uint8_t *display_get_bw_buffer(void)  { return bw_buf; }
uint8_t *display_get_red_buffer(void) { return red_buf; }

void display_clear(void) {
    memset(bw_buf,  0xFF, EPD_BUF_SIZE);
    memset(red_buf, 0x00, EPD_BUF_SIZE);
    display_full_refresh();
}

void display_sleep(void) {
    spi_cmd(CMD_DEEP_SLEEP);
    spi_data(0xA5);   // check code
}

void display_wake(void) {
    _hw_reset();
    _ssd1677_init();
}
