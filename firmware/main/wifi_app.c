/*
 * wifi_app.c — WiFi STA + MQTT TLS 客户端
 *
 * 配置通过 menuconfig 或代码常量注入。
 * 生产环境应从 NVS 读取 WiFi 密码和 MQTT 凭据。
 */

#include "wifi_app.h"
#include "pin_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_tls.h"

static const char *TAG = "wifi_mqtt";

/* ⚠️ 编译前替换为实际凭据 */
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
#define MQTT_BROKER    "mqtts://beelzebub.top:8883"
#define MQTT_USER      "desk"
#define MQTT_PASS      "YOUR_MQTT_PASSWORD"
#define MQTT_CLIENT_ID "ink-player-desk"

static esp_mqtt_client_handle_t mqtt_client;
extern QueueHandle_t mqtt_queue;
extern QueueHandle_t enc_queue;

/* ── WiFi 事件回调 ── */
static void wifi_event_handler(void *arg,
    esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi connecting...");
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retry...");
        esp_wifi_connect();
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected, got IP");
        mqtt_app_start();
    }
}

/* ── WiFi 初始化 ── */
void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA init done (SSID: %s)", WIFI_SSID);
}

/* ── MQTT 事件回调 ── */
static void mqtt_event_handler(void *arg,
    esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t evt = data;

    switch (id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_subscribe(mqtt_client, "desk/display", 0);
        esp_mqtt_client_subscribe(mqtt_client, "desk/alert", 0);
        esp_mqtt_client_subscribe(mqtt_client, "desk/reminder", 0);
        /* 上线通知 */
        esp_mqtt_client_publish(mqtt_client, "desk/online",
            "{\"status\":\"online\"}", 0, 0, 0);
        break;

    case MQTT_EVENT_DATA: {
        mqtt_msg_t msg;
        snprintf(msg.topic, sizeof(msg.topic), "%.*s",
                 evt->topic_len, evt->topic);
        int plen = evt->data_len < sizeof(msg.payload)-1
                   ? evt->data_len : sizeof(msg.payload)-1;
        memcpy(msg.payload, evt->data, plen);
        msg.payload[plen] = '\0';
        msg.len = plen;
        xQueueSend(mqtt_queue, &msg, 0);
        ESP_LOGI(TAG, "MQTT rx: %s", msg.topic);
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    default:
        break;
    }
}

/* ── MQTT 客户端 ── */
void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
        .credentials = {
            .username = MQTT_USER,
            .authentication.password = MQTT_PASS,
            .client_id = MQTT_CLIENT_ID,
        },
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client,
        ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void mqtt_publish(const char *topic, const char *payload)
{
    if (mqtt_client) {
        esp_mqtt_client_publish(mqtt_client, topic,
            payload, 0, 0, 0);
    }
}

/* ── WiFi+MQTT 任务 ── */
void task_wifi_mqtt(void *pv)
{
    wifi_init_sta();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));  // 心跳
    }
}
