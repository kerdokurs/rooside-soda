#include "wifi.h"

#include <esp_wifi.h>
#include <esp_check.h>

#include <string.h>

#define WIFI_LOG_TAG "[WIFI]"

static esp_event_handler_t g_wifi_event_handler;

esp_err_t midi_wifi_init(const wifi_credentials_t *credentials) {
    ESP_RETURN_ON_ERROR(esp_netif_init(), WIFI_LOG_TAG, "Error initializing netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), WIFI_LOG_TAG, "Error creating default event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), WIFI_LOG_TAG, "Error initializing wifi driver");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, credentials->wifi_event_handler, NULL), WIFI_LOG_TAG, "Error registering wifi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, credentials->wifi_event_handler, NULL), WIFI_LOG_TAG, "Error initializing IP event handler");

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    strcpy((char*)&wifi_config.sta.ssid, credentials->ssid);
    strcpy((char*)&wifi_config.sta.password, credentials->passwd);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), WIFI_LOG_TAG, "Error setting wifi mode to STA");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config), WIFI_LOG_TAG, "Error setting wifi STA config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), WIFI_LOG_TAG, "Error starting wifi driver");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), WIFI_LOG_TAG, "Error connecting to wifi");

    g_wifi_event_handler = credentials->wifi_event_handler;

    return ESP_OK;
}

esp_err_t midi_wifi_deinit(void) {
    ESP_RETURN_ON_ERROR(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, g_wifi_event_handler), WIFI_LOG_TAG, "Error removing wifi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, g_wifi_event_handler), WIFI_LOG_TAG, "Error removing IP event handler");
    ESP_RETURN_ON_ERROR(esp_wifi_disconnect(), WIFI_LOG_TAG, "Error disconnecting wifi");
    ESP_RETURN_ON_ERROR(esp_wifi_stop(), WIFI_LOG_TAG, "Error stopping wifi driver");
    ESP_RETURN_ON_ERROR(esp_wifi_deinit(), WIFI_LOG_TAG, "Error deinitializing wifi driver");

    g_wifi_event_handler = NULL;

    return ESP_OK;
}

