/*
 * display.h — SSD1677 墨水屏驱动
 * HINK-E075A07-A0, 800×480, 三色 (BW+R)
 */

#pragma once
#include <stdint.h>
#include "pin_config.h"

/* 刷新模式 */
typedef enum {
    FULL_UPDATE  = 0,   // 全刷 3-5s, 无残影
    PARTIAL_UPDATE = 1, // 局刷 0.3-0.8s, 轻度残影
} update_mode_t;

void display_init(void);
void display_sleep(void);
void display_wake(void);

/* 全屏刷新: 先写 buffer 再刷 */
void display_full_refresh(void);

/* 窗口局刷: 只更新指定区域 (x,y,w,h) */
void display_partial_refresh(int x, int y, int w, int h);

/* 获取帧缓冲指针 (写入后调用刷新函数) */
uint8_t *display_get_bw_buffer(void);   // 黑/白
uint8_t *display_get_red_buffer(void);  // 红

/* 直接清屏 */
void display_clear(void);
