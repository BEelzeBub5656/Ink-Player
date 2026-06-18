/*
 * ui_render.h — UI 渲染器
 */

#pragma once
#include "menu.h"

/* 页面绘制函数 */
void ui_draw_splash(void);
void ui_draw_statusbar(const char *time_str, int wifi_on, int bat_pct);
void ui_draw_menu(const menu_state_t *m);
void ui_draw_page(menu_state_t *m);

/* 任务: 根据菜单状态刷新屏幕 */
void task_menu(void *pv);
void task_display(void *pv);
void task_idle_timer(void *pv);
