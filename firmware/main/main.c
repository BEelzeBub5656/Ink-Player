/*
 * main.c — Ink-Player 主入口
 * FreeRTOS 多任务: 编码器→菜单→屏幕→WiFi/MQTT→音频
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "pin_config.h"
#include "encoder.h"
#include "display.h"
#include "menu.h"
#include "ui_render.h"

static const char *TAG = "main";

/* ── 消息队列 ── */
QueueHandle_t enc_queue;    // encoder_event_t
QueueHandle_t mqtt_queue;   // mqtt_msg_t

/* ── 共享状态 ── */
menu_state_t g_menu;

/* ── 任务原型 ── */
void task_encoder(void *pv);
void task_menu(void *pv);
void task_display(void *pv);
void task_wifi_mqtt(void *pv);
void task_audio(void *pv);
void task_idle_timer(void *pv);

void app_main(void)
{
    ESP_LOGI(TAG, "Ink-Player booting...");

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 消息队列 */
    enc_queue  = xQueueCreate(16, sizeof(encoder_event_t));
    mqtt_queue = xQueueCreate(8,  sizeof(mqtt_msg_t));

    /* 硬件初始化 */
    display_init();
    encoder_init();
    menu_init(&g_menu);

    /* 启动画面 */
    ui_draw_splash();
    display_full_refresh();
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* 创建 FreeRTOS 任务 (按优先级) */
    xTaskCreate(task_encoder,   "encoder",   4096, NULL, 5, NULL);
    xTaskCreate(task_menu,      "menu",      4096, NULL, 4, NULL);
    xTaskCreate(task_wifi_mqtt, "wifi_mqtt", 8192, NULL, 3, NULL);
    xTaskCreate(task_display,   "display",   4096, NULL, 2, NULL);
    xTaskCreate(task_audio,     "audio",     4096, NULL, 1, NULL);
    xTaskCreate(task_idle_timer,"idle",      2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "All tasks started");

    /* 主循环闲置 */
    while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
}
