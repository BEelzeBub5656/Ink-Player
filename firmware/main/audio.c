/*
 * audio.c — I2S 音频驱动
 * INMP441 麦克风 (输入) + MAX98357A 功放 (输出)
 *
 * 共享 I2S 总线, 半双工: 同一时间只能收或发
 */

#include "audio.h"
#include "pin_config.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "audio";
static i2s_chan_handle_t rx_chan = NULL;
static i2s_chan_handle_t tx_chan = NULL;

void audio_init(void)
{
    /* I2S 标准模式, 16kHz 16bit mono */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    /* RX 通道 (INMP441) */
    i2s_std_config_t rx_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws   = I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DIN,
        },
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, &rx_chan));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_cfg));

    /* TX 通道 (MAX98357A) */
    i2s_std_config_t tx_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws   = I2S_WS,
            .dout = I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_cfg));

    /* MAX98357A 使能 */
    gpio_set_direction(AMP_SD, GPIO_MODE_OUTPUT);
    gpio_set_level(AMP_SD, 1);  // SD_MODE=高, 功放开

    ESP_LOGI(TAG, "I2S audio ready (16kHz, mono)");
}

void audio_capture_start(void)
{
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

void audio_capture_stop(void)
{
    ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));
}

void audio_play(uint8_t *pcm, int len)
{
    size_t written;
    i2s_channel_write(tx_chan, pcm, len, &written, portMAX_DELAY);
}

/* ── 音频任务 ── */
void task_audio(void *pv)
{
    audio_init();
    ESP_LOGI(TAG, "Audio task idle (doubao not yet connected)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
