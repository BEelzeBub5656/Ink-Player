/*
 * display.c — SSD1677 墨水屏驱动 (SPI)
 *
 * HINK-E075A07-A0 7.5" 三色面板
 * 800×480, 4 级灰度 (BW), 1 级红色
 *
 * 关键时序:
 *   - 全刷: ~3-5s   (full LUT)
 *   - 局刷: ~0.3-1s (partial LUT)
 *   - 每 10 次局刷后自动全刷清残影
 */

#include "display.h"
#include "pin_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

static spi_device_handle_t spi;
static uint8_t *bw_buf   = NULL;  // 48000 bytes (PSRAM)
static uint8_t *red_buf  = NULL;  // 48000 bytes
static int partial_count = 0;     // 局刷计数器

/* ── SSD1677 命令 ── */
#define CMD_DRIVER_OUTPUT   0x01
#define CMD_GATE_VOLTAGE    0x03
#define CMD_DEEP_SLEEP      0x10
#define CMD_DATA_ENTRY      0x11
#define CMD_SW_RESET        0x12
#define CMD_TEMP_SENSOR     0x18
#define CMD_MASTER_ACTIVATE 0x20
#define CMD_DISP_UPDATE_1   0x21
#define CMD_DISP_UPDATE_2   0x22
#define CMD_WRITE_BW        0x24
#define CMD_WRITE_RED       0x26
#define CMD_VCOM            0x2C
#define CMD_BORDER          0x3C
#define CMD_SET_RAMX        0x44
#define CMD_SET_RAMY        0x45

/* ── 辅助宏 ── */
#define spi_cmd(cmd)       _spi_send_cmd(cmd)
#define spi_data(data)     _spi_send_data(data)
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
        .length = len * 8,
        .tx_buffer = buf,
    };
    spi_device_transmit(spi, &t);
}

/* ── SPI 初始化 ── */
void display_init(void)
{
    /* GPIO 配置 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << EPD_DC) | (1ULL << EPD_RST) | (1ULL << EPD_BUSY),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(EPD_DC, 0);
    gpio_set_level(EPD_CS, 1);

    /* BUSY = 输入 (从屏读忙信号) */
    gpio_set_direction(EPD_BUSY, GPIO_MODE_INPUT);
    gpio_set_pull_mode(EPD_BUSY, GPIO_PULLUP_ONLY);

    /* SPI 总线 */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = EPD_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = EPD_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = EPD_WIDTH * EPD_HEIGHT / 8,  // 48KB
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 20 * 1000 * 1000,  // 20 MHz
        .mode           = 0,
        .spics_io_num   = EPD_CS,
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi));

    /* 帧缓冲 (PSRAM) */
    bw_buf  = heap_caps_malloc(EPD_BUF_SIZE, MALLOC_CAP_SPIRAM);
    red_buf = heap_caps_malloc(EPD_BUF_SIZE, MALLOC_CAP_SPIRAM);
    assert(bw_buf && red_buf);
    memset(bw_buf,  0xFF, EPD_BUF_SIZE);  // 全白
    memset(red_buf, 0x00, EPD_BUF_SIZE);  // 无红

    /* 硬件复位 */
    gpio_set_level(EPD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    while (gpio_get_level(EPD_BUSY) == 0) { vTaskDelay(1); }

    /* SSD1677 初始化序列 */
    spi_cmd(CMD_SW_RESET);
    vTaskDelay(pdMS_TO_TICKS(10));
    while (gpio_get_level(EPD_BUSY) == 0) { vTaskDelay(1); }

    // 数据进入模式: X 递增, Y 递减
    spi_cmd(CMD_DATA_ENTRY);
    spi_data(0x03);  // X inc, Y dec

    // RAM 地址范围
    spi_cmd(CMD_SET_RAMX);
    spi_data(0x00);  // X start
    spi_data(((EPD_WIDTH - 1) >> 3) & 0xFF);  // X end (99 = 800/8-1)

    spi_cmd(CMD_SET_RAMY);
    spi_data(0x00); spi_data(0x00);  // Y start
    spi_data((EPD_HEIGHT - 1) & 0xFF);
    spi_data(((EPD_HEIGHT - 1) >> 8) & 0xFF);  // Y end

    // 边框 = 白
    spi_cmd(CMD_BORDER);
    spi_data(0x01);  // 0=黑, 1=白, 2=红

    // VCOM
    spi_cmd(CMD_VCOM);
    spi_data(0x70);  // -1.5V

    ESP_LOGI(TAG, "SSD1677 800×480 ready");
}

/* ── 等待 BUSY 释放 ── */
static void _wait_busy(void) {
    while (gpio_get_level(EPD_BUSY) == 1) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ── 全屏刷新 ── */
void display_full_refresh(void)
{
    partial_count = 0;

    /* 写 BW 缓冲 */
    spi_cmd(CMD_WRITE_BW);
    spi_data_buf(bw_buf, EPD_BUF_SIZE);

    /* 写 RED 缓冲 */
    spi_cmd(CMD_WRITE_RED);
    spi_data_buf(red_buf, EPD_BUF_SIZE);

    /* 触发刷新 */
    spi_cmd(CMD_DISP_UPDATE_2);
    spi_data(0xC7);  // full LUT, 全刷波形

    spi_cmd(CMD_MASTER_ACTIVATE);
    _wait_busy();

    ESP_LOGD(TAG, "Full refresh done");
}

/* ── 窗口局部刷新 ── */
void display_partial_refresh(int x, int y, int w, int h)
{
    partial_count++;

    /* 每 10 次局刷, 强制全刷清残影 */
    if (partial_count >= 10) {
        display_full_refresh();
        return;
    }

    /* 设置窗口 */
    spi_cmd(CMD_SET_RAMX);
    spi_data((x >> 3) & 0xFF);
    spi_data(((x + w - 1) >> 3) & 0xFF);

    spi_cmd(CMD_SET_RAMY);
    spi_data(y & 0xFF);
    spi_data((y >> 8) & 0xFF);
    spi_data((y + h - 1) & 0xFF);
    spi_data(((y + h - 1) >> 8) & 0xFF);

    /* 写数据 (只写窗口内的) */
    int bytes_per_row = (w + 7) / 8;
    spi_cmd(CMD_DATA_ENTRY);
    spi_data(0x03);  // X inc, Y dec

    spi_cmd(CMD_WRITE_BW);
    for (int row = y; row < y + h; row++) {
        int offset = row * (EPD_WIDTH / 8) + (x / 8);
        spi_data_buf(bw_buf + offset, bytes_per_row);
    }

    /* 触发局刷 */
    spi_cmd(CMD_DISP_UPDATE_2);
    spi_data(0x0F);  // partial LUT, 局刷波形

    spi_cmd(CMD_MASTER_ACTIVATE);
    _wait_busy();

    ESP_LOGD(TAG, "Partial refresh (%d,%d %dx%d) #%d", x, y, w, h, partial_count);
}

/* ── 缓冲访问 ── */
uint8_t *display_get_bw_buffer(void)  { return bw_buf; }
uint8_t *display_get_red_buffer(void) { return red_buf; }

/* ── 清屏 ── */
void display_clear(void) {
    memset(bw_buf,  0xFF, EPD_BUF_SIZE);
    memset(red_buf, 0x00, EPD_BUF_SIZE);
    display_full_refresh();
}

/* ── 休眠/唤醒 ── */
void display_sleep(void) {
    spi_cmd(CMD_DEEP_SLEEP);
    spi_data(0x01);  // mode 1
}

void display_wake(void) {
    gpio_set_level(EPD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    _wait_busy();
}
