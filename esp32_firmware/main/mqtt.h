#pragma once

#include <esp_err.h>
#include <esp_event_base.h>

#include <freertos/FreeRTOS.h>

#include <stdbool.h>

typedef struct {
    char host[64];
    char username[32];
    char passwd[32];
    char topic[32];
} midi_mqtt_config_t;

esp_err_t midi_mqtt_init(midi_mqtt_config_t *config);
esp_err_t midi_mqtt_deinit(void);

typedef struct {
    int *button;
    int *ticks;
} midi_mqtt_publish_data_t;

esp_err_t midi_mqtt_publish(midi_mqtt_publish_data_t *data);

