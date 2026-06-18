/*
 * encoder.h — 旋转编码器 (PCNT 硬件解码)
 * CW=下一项, CCW=确认
 */

#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    ENC_NONE = 0,
    ENC_CW,        // 顺时针 → 下一项
    ENC_CCW,       // 逆时针 → 确认
} encoder_event_t;

void encoder_init(void);
