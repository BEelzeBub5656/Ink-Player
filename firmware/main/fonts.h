/*
 * fonts.h — 8×16 ASCII 点阵字体
 */

#pragma once
#include <stdint.h>

/* 8×16 像素, 每字符 16 bytes */
extern const uint8_t font8x16[][16];

/* 字体函数 */
void font_draw_char(uint8_t *buf, int x, int y,
                    char c, int buf_w, int inverted);
void font_draw_string(uint8_t *buf, int x, int y,
                      const char *str, int buf_w, int inverted);
