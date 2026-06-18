/*
 * menu.h — 菜单状态机
 * CW=下一项 / CCW=确认 / 10s 超时回桌面
 */

#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define MAX_MENU_ITEMS  8
#define MAX_TITLE_LEN   32
#define IDLE_TIMEOUT_MS 10000

/* 页面类型 */
typedef enum {
    PAGE_IDLE,      // 待机桌面
    PAGE_MAIN_MENU, // 主菜单
    PAGE_WEATHER,   // 天气
    PAGE_SCHEDULE,  // 日程
    PAGE_MUSIC,     // 音乐控制
    PAGE_SETTINGS,  // 设置
    PAGE_DIARY,     // 日记/日报
    PAGE_COUNT,
} page_id_t;

/* 菜单项 */
typedef struct {
    char     title[MAX_TITLE_LEN];
    page_id_t target;
} menu_item_t;

/* 菜单状态 */
typedef struct {
    page_id_t current_page;
    int       selected_idx;   // 当前高亮项
    int       item_count;     // 当前页菜单项数
    menu_item_t items[MAX_MENU_ITEMS];
    int64_t   last_activity;  // 最后操作时间 (us)
} menu_state_t;

/* ── API ── */
void menu_init(menu_state_t *m);
void menu_handle_event(menu_state_t *m, int event);  // ENC_CW=0 / ENC_CCW=1
void menu_enter_page(menu_state_t *m, page_id_t page);
void menu_go_back(menu_state_t *m);
int  menu_check_idle(menu_state_t *m);  // 1=超时应回桌面
