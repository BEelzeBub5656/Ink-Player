/*
 * audio.h — I2S 音频 (INMP441 + MAX98357A) + 豆包 WS 占位
 */

#pragma once

void audio_init(void);
void audio_capture_start(void);
void audio_capture_stop(void);
void audio_play(uint8_t *pcm, int len);

/* 豆包 WebSocket (存根 — 等 API key 后填充) */
void doubao_ws_init(void);
void doubao_ws_send_audio(const uint8_t *pcm, int len);
