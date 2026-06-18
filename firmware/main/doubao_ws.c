/*
 * doubao_ws.c — 豆包端到端实时语音 WebSocket 客户端 (占位)
 *
 * 参考: doubao-realtime-voice skill
 * 协议: WebSocket 二进制帧
 *   - 上行: PCM 16kHz 16bit mono → 豆包
 *   - 下行: PCM 16kHz 16bit mono ← 豆包
 *
 * TODO: 填入 API Key + 实现 WebSocket 握手 + 音频流收发
 * PCB 焊完后加载 doubao-realtime-voice skill 补全实现
 */

#include "audio.h"
#include "esp_log.h"

static const char *TAG = "doubao";

/* ⚠️ 占位 — 编译通过, 运行时返回空 */
void doubao_ws_init(void)
{
    ESP_LOGW(TAG, "Doubao WS not implemented — waiting for API key");
}

void doubao_ws_send_audio(const uint8_t *pcm, int len)
{
    (void)pcm; (void)len;
    ESP_LOGW(TAG, "Doubao send not implemented");
}

/* 兜底: 无豆包时的本地 TTS fallback */
void task_audio(void *pv) {
    audio_init();
    while (1) { vTaskDelay(pdMS_TO_TICKS(60000)); }
}
