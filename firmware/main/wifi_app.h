/*
 * wifi_app.h / mqtt_app.h — WiFi + MQTT TLS 客户端
 */

#pragma once
#include <stdint.h>

/* MQTT 下行消息 */
typedef struct {
    char topic[64];
    char payload[512];
    int  len;
} mqtt_msg_t;

void wifi_init_sta(void);
void mqtt_app_start(void);
void mqtt_publish(const char *topic, const char *payload);
