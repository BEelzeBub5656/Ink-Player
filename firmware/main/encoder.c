/*
 * encoder.c — PCNT 硬件正交解码 + 去抖
 *
 * SIQ-02FVS3:
 *   A → IO35 (+10k↑ + 10nF→GND)
 *   B → IO36 (+10k↑ + 10nF→GND)
 *   C → GND
 *   SW → 未连接
 *
 * 交互逻辑:
 *   CW (count++)  = 移动到下一项
 *   CCW (count--) = 确认当前项
 */

#include "encoder.h"
#include "pin_config.h"
#include "driver/pcnt.h"
#include "esp_log.h"

static const char *TAG = "encoder";

static pcnt_unit_handle_t pcnt_unit;
extern QueueHandle_t enc_queue;

void encoder_init(void)
{
    /* PCNT 单元配置 */
    pcnt_unit_config_t unit_cfg = {
        .low_limit  = -100,
        .high_limit = 100,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &pcnt_unit));

    /* 通道配置: A=边沿, B=电平 (4 倍频) */
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num  = ENC_A,   // IO35 — 计数边沿
        .level_gpio_num = ENC_B,   // IO36 — 方向判定
    };
    pcnt_channel_handle_t pcnt_chan;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_cfg, &pcnt_chan));

    /* 上升+下降都计数 (4 倍频) */
    pcnt_channel_set_edge_action(pcnt_chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE);

    /* B 高电平时计数方向为正 */
    pcnt_channel_set_level_action(pcnt_chan,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    /* 启动 PCNT */
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    /* 消抖: 10nF + 10kΩ ≈ 1.6kHz 截止, 软件再 50ms 去抖 */
    ESP_LOGI(TAG, "PCNT encoder ready (IO35=A, IO36=B)");
}

/* ── 任务: 轮询 PCNT, 发送事件 ── */
void task_encoder(void *pv)
{
    int count, last_count = 0;
    encoder_event_t evt;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms 去抖窗口

        pcnt_unit_get_count(pcnt_unit, &count);
        int diff = count - last_count;

        /* 累积超过 2 步才触发 (防毛刺) */
        if (diff >= 2) {
            evt = ENC_CW;
            xQueueSend(enc_queue, &evt, 0);
            ESP_LOGD(TAG, "CW  (diff=%d)", diff);
        } else if (diff <= -2) {
            evt = ENC_CCW;
            xQueueSend(enc_queue, &evt, 0);
            ESP_LOGD(TAG, "CCW (diff=%d)", diff);
        }

        /* 同步计数, 避免累积漂移 */
        if (abs(diff) >= 2) {
            pcnt_unit_clear_count(pcnt_unit);
            count = 0;
        }
        last_count = count;
    }
}
