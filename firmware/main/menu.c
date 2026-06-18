/*
 * menu.c — 菜单状态机实现
 *
 * 交互规则:
 *   CW   = 移动到下一项 (循环)
 *   CCW  = 确认/进入当前项
 *   10s 无操作 = 回桌面
 *
 * 菜单结构:
 *   桌面 → (任意转动) → 主菜单 → 子页面 → 主菜单 → (10s) → 桌面
 */

#include "menu.h"
#include "encoder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "menu";

void menu_init(menu_state_t *m)
{
    memset(m, 0, sizeof(*m));
    m->current_page  = PAGE_IDLE;
    m->selected_idx  = 0;
    m->last_activity = esp_timer_get_time();

    /* 主菜单项 */
    menu_item_t main_items[] = {
        {"天气",    PAGE_WEATHER},
        {"日程",    PAGE_SCHEDULE},
        {"音乐",    PAGE_MUSIC},
        {"设置",    PAGE_SETTINGS},
        {"日记",    PAGE_DIARY},
    };
    memcpy(m->items, main_items, sizeof(main_items));
    m->item_count = 5;

    ESP_LOGI(TAG, "Menu init: IDLE, %d main items", m->item_count);
}

void menu_handle_event(menu_state_t *m, int evt)
{
    m->last_activity = esp_timer_get_time();

    if (m->current_page == PAGE_IDLE) {
        /* 桌面: 任意转动 → 进主菜单 */
        if (evt == ENC_CW) {
            menu_enter_page(m, PAGE_MAIN_MENU);
        }
        /* CCW 在桌面无效 */
        return;
    }

    if (evt == ENC_CW) {
        /* CW: 下一项 */
        m->selected_idx = (m->selected_idx + 1) % m->item_count;
        ESP_LOGD(TAG, "Select: %d/%d — %s",
                 m->selected_idx, m->item_count,
                 m->items[m->selected_idx].title);
    }
    else if (evt == ENC_CCW) {
        /* CCW: 确认 */
        menu_item_t *item = &m->items[m->selected_idx];

        if (item->target == PAGE_MAIN_MENU) {
            /* 返回主菜单 */
            menu_enter_page(m, PAGE_MAIN_MENU);
        } else {
            /* 进入子页面 */
            menu_enter_page(m, item->target);
        }
    }
}

void menu_enter_page(menu_state_t *m, page_id_t page)
{
    m->current_page = page;
    m->selected_idx = 0;

    /* 根据页面构建菜单项 */
    switch (page) {
    case PAGE_MAIN_MENU: {
        menu_item_t items[] = {
            {"天气",  PAGE_WEATHER},
            {"日程",  PAGE_SCHEDULE},
            {"音乐",  PAGE_MUSIC},
            {"设置",  PAGE_SETTINGS},
            {"日记",  PAGE_DIARY},
        };
        memcpy(m->items, items, sizeof(items));
        m->item_count = 5;
        break;
    }
    case PAGE_SETTINGS: {
        menu_item_t items[] = {
            {"WiFi 信息",  PAGE_SETTINGS},
            {"OTA 升级",   PAGE_SETTINGS},
            {"关于",       PAGE_SETTINGS},
            {"← 返回",     PAGE_MAIN_MENU},
        };
        memcpy(m->items, items, sizeof(items));
        m->item_count = 4;
        break;
    }
    case PAGE_IDLE:
    default:
        m->item_count = 0;
        break;
    }

    ESP_LOGI(TAG, "Enter page %d, %d items", page, m->item_count);
}

void menu_go_back(menu_state_t *m)
{
    menu_enter_page(m, PAGE_MAIN_MENU);
}

int menu_check_idle(menu_state_t *m)
{
    if (m->current_page == PAGE_IDLE) return 0;
    int64_t now = esp_timer_get_time();
    if ((now - m->last_activity) > IDLE_TIMEOUT_MS * 1000) {
        m->current_page = PAGE_IDLE;
        ESP_LOGI(TAG, "Idle timeout → IDLE");
        return 1;
    }
    return 0;
}
