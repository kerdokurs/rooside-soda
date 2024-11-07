#pragma once

#include <esp_err.h>
#include <esp_event_base.h>

typedef struct {
    char ssid[32];
    char passwd[64];
    esp_event_handler_t wifi_event_handler;
} wifi_credentials_t;

esp_err_t midi_wifi_init(const wifi_credentials_t *credentials);
esp_err_t midi_wifi_deinit(void);

int is_mqtt_connected(void);
