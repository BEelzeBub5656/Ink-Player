/*
 * ui_render.c — UI 渲染器
 *
 * 在帧缓冲中绘制, 调用 display_* 函数触发物理刷新。
 * 状态栏用局部刷新, 菜单/页面用全刷。
 */

#include "ui_render.h"
#include "fonts.h"
#include "display.h"
#include "pin_config.h"
#include "encoder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui";

extern QueueHandle_t enc_queue;
extern QueueHandle_t mqtt_queue;
extern menu_state_t g_menu;

/* ── 启动画面 ── */
void ui_draw_splash(void)
{
    uint8_t *buf  = display_get_bw_buffer();
    uint8_t *red  = display_get_red_buffer();
    memset(buf, 0xFF, EPD_BUF_SIZE);  // 全白
    memset(red, 0x00, EPD_BUF_SIZE);  // 无红

    font_draw_string(buf, EPD_WIDTH/2 - 60, EPD_HEIGHT/2 - 24,
                     "Ink-Player", EPD_WIDTH, 0);
    font_draw_string(buf, EPD_WIDTH/2 - 80, EPD_HEIGHT/2 + 8,
                     "Starting...", EPD_WIDTH, 0);
}

/* ── 状态栏 (顶部 40px) ── */
void ui_draw_statusbar(const char *time_str, int wifi_on, int bat_pct)
{
    uint8_t *buf = display_get_bw_buffer();
    uint8_t *red = display_get_red_buffer();
    char sb[64];

    /* 清状态栏区域 (BW + 红) */
    for (int row = 0; row < STATUSBAR_H; row++) {
        memset(buf + row * (EPD_WIDTH/8), 0xFF, EPD_WIDTH/8);
        memset(red + row * (EPD_WIDTH/8), 0x00, EPD_WIDTH/8);
    }

    /* 底部横线 */
    for (int col = 0; col < EPD_WIDTH; col++) {
        int byte_idx = (STATUSBAR_H - 2) * (EPD_WIDTH/8) + (col/8);
        int bit_idx  = 7 - (col % 8);
        buf[byte_idx] &= ~(1 << bit_idx);
    }

    /* 时间 (左对齐) */
    font_draw_string(buf, 4, 4, time_str, EPD_WIDTH, 0);

    /* 电池 (右对齐) */
    snprintf(sb, sizeof(sb), "%d%%", bat_pct);
    font_draw_string(buf, EPD_WIDTH - 40 - 4, 4, sb, EPD_WIDTH, 0);

    /* WiFi 图标 (右侧) */
    font_draw_string(buf, EPD_WIDTH - 64 - 4, 4,
                     wifi_on ? "W" : " ", EPD_WIDTH, 0);
}

/* ── 菜单列表 ── */
void ui_draw_menu(const menu_state_t *m)
{
    uint8_t *buf = display_get_bw_buffer();
    uint8_t *red = display_get_red_buffer();
    memset(buf, 0xFF, EPD_BUF_SIZE);
    memset(red, 0x00, EPD_BUF_SIZE);  // 无红

    /* 标题 */
    char title[32];
    switch (m->current_page) {
    case PAGE_MAIN_MENU: strcpy(title, "Menu");   break;
    case PAGE_SETTINGS:  strcpy(title, "Settings"); break;
    case PAGE_WEATHER:   strcpy(title, "Weather");  break;
    case PAGE_SCHEDULE:  strcpy(title, "Schedule"); break;
    case PAGE_MUSIC:     strcpy(title, "Music");    break;
    case PAGE_DIARY:     strcpy(title, "Diary");    break;
    default:             strcpy(title, "Ink-Player"); break;
    }

    /* 标题栏 */
    font_draw_string(buf, 8, 50, title, EPD_WIDTH, 0);
    /* 下划线 */
    for (int col = 0; col < 200; col++) {
        int byte_idx = 66 * (EPD_WIDTH/8) + (col/8);
        int bit_idx  = 7 - (col % 8);
        buf[byte_idx] &= ~(1 << bit_idx);
    }

    /* 菜单项 */
    int y = 80;
    for (int i = 0; i < m->item_count; i++) {
        int inverted = (i == m->selected_idx);
        char label[64];
        snprintf(label, sizeof(label), "%s %s",
                 inverted ? ">" : " ",
                 m->items[i].title);
        font_draw_string(buf, 16, y, label, EPD_WIDTH, inverted);
        y += 32;  // 项间距
    }
}

/* ── 页面渲染 ── */
void ui_draw_page(menu_state_t *m)
{
    uint8_t *buf = display_get_bw_buffer();
    uint8_t *red = display_get_red_buffer();
    memset(buf, 0xFF, EPD_BUF_SIZE);
    memset(red, 0x00, EPD_BUF_SIZE);  // 无红

    switch (m->current_page) {
    case PAGE_WEATHER:
        font_draw_string(buf, 8, 50, "Weather", EPD_WIDTH, 0);
        font_draw_string(buf, 8, 80, "Loading from MQTT...", EPD_WIDTH, 0);
        break;
    case PAGE_SCHEDULE:
        font_draw_string(buf, 8, 50, "Schedule", EPD_WIDTH, 0);
        font_draw_string(buf, 8, 80, "No events today", EPD_WIDTH, 0);
        break;
    case PAGE_MUSIC:
        font_draw_string(buf, 8, 50, "Music Control", EPD_WIDTH, 0);
        font_draw_string(buf, 8, 80, "Not connected", EPD_WIDTH, 0);
        break;
    case PAGE_DIARY:
        font_draw_string(buf, 8, 50, "Daily Digest", EPD_WIDTH, 0);
        font_draw_string(buf, 8, 80, "Pulling from MQTT...", EPD_WIDTH, 0);
        break;
    case PAGE_SETTINGS:
        font_draw_string(buf, 8, 50, "Settings", EPD_WIDTH, 0);
        font_draw_string(buf, 8, 80, "Firmware: v1.0.0", EPD_WIDTH, 0);
        break;
    default:
        break;
    }
}

/* ── FreeRTOS 任务 ── */

/* 菜单任务: 读编码器 → 更新状态 → 触发渲染 */
void task_menu(void *pv)
{
    encoder_event_t evt;
    int last_page = g_menu.current_page;

    while (1) {
        if (xQueueReceive(enc_queue, &evt, pdMS_TO_TICKS(200)) == pdTRUE) {
            menu_handle_event(&g_menu, evt);

            /* 页面变化 → 全屏刷新 */
            if (g_menu.current_page != last_page) {
                if (g_menu.current_page == PAGE_IDLE) {
                    /* 回桌面 */
                    ui_draw_statusbar("--:--", 0, 100);
                    display_partial_refresh(0, 0, EPD_WIDTH, STATUSBAR_H);
                } else if (g_menu.item_count > 0) {
                    ui_draw_menu(&g_menu);
                    display_full_refresh();
                } else {
                    ui_draw_page(&g_menu);
                    display_full_refresh();
                }
                last_page = g_menu.current_page;
            }
            /* 仅选中项变化 → 局部重绘高亮 */
            else if (g_menu.current_page != PAGE_IDLE && g_menu.item_count > 0) {
                ui_draw_menu(&g_menu);
                display_partial_refresh(0, 80, 320, g_menu.item_count * 32);
            }
        }
    }
}

/* 显示任务: 状态栏定时刷新 */
void task_display(void *pv)
{
    while (1) {
        if (g_menu.current_page == PAGE_IDLE) {
            /* 桌面: 每 30 秒刷状态栏时间 */
            ui_draw_statusbar("--:--", 0, 100);
            display_partial_refresh(0, 0, EPD_WIDTH, STATUSBAR_H);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* 空闲检测任务: 10s 无操作 → 回桌面 */
void task_idle_timer(void *pv)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (menu_check_idle(&g_menu)) {
            /* 回桌面 */
            ui_draw_statusbar("--:--", 0, 100);
            display_full_refresh();
        }
    }
}
