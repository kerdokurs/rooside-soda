#include "mqtt.h"

#include <mqtt_client.h>
#include <esp_log.h>
#include <esp_check.h>

#include <freertos/task.h>

#include <string.h>
#include <stdio.h>

#define MQTT_LOG_TAG "[MQTT]"

static esp_mqtt_client_handle_t g_mqtt_handle;

#define MQTT_TOPIC_LENGTH 32
static char g_mqtt_topic[32];

#define MQTT_PUBLISH_QUEUE_SIZE 32
#define MQTT_PUBLISH_TASK_NAME "MQTT_PUBLISH_TASK"

#define MQTT_CONSUME_QUEUE_SIZE 32

static TaskHandle_t g_mqtt_publish_task;
static QueueHandle_t g_mqtt_publish_queue;

#define MQTT_BUFFER_SIZE 64

static void mqtt_publish_handler(void* a) {
    (void)a;

    midi_mqtt_publish_data_t data;

    while (g_mqtt_publish_queue != NULL) {
        if (xQueueReceive(g_mqtt_publish_queue, &data, portMAX_DELAY)) {
            ESP_LOGI(MQTT_LOG_TAG, "Got data: %d, time: %d", *data.button, *data.ticks);

            char buf[MQTT_BUFFER_SIZE];
            memset(buf, 0, MQTT_BUFFER_SIZE);
            snprintf(buf, MQTT_BUFFER_SIZE, "%d", *data.ticks);
            size_t len = strlen(buf);

            char topic[MQTT_TOPIC_LENGTH * 2];
            snprintf(topic, MQTT_TOPIC_LENGTH * 2, "%s/%c", g_mqtt_topic, *data.button == 0 ? '0' : '1');

            esp_err_t err = esp_mqtt_client_publish(g_mqtt_handle, topic, buf, len, 0, 1);
            if (err != ESP_OK) {
                ESP_LOGE(MQTT_LOG_TAG, "Error publishing mqtt data: %s", esp_err_to_name(err));
            }
            
            vPortFree(data.button);
            vPortFree(data.ticks);
        }
    }

    ESP_LOGE(MQTT_LOG_TAG, "Error: mqtt publish queue is NULL. Idling");
    for (;;) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static volatile int g_is_connected = 0;

static void mqtt_event_handler(void *handler_arg, esp_event_base_t base, int32_t event_id, void *data) {
    if(event_id == MQTT_EVENT_CONNECTED) {
        g_is_connected = 1;
        printf("MQTT connected\n");
    } else if(event_id == MQTT_EVENT_DISCONNECTED) {
        printf("MQTT disconnected\n");
    } else if(event_id == MQTT_EVENT_SUBSCRIBED) {
        printf("MQTT event subscribed\n");
    } else if(event_id == MQTT_EVENT_UNSUBSCRIBED){
        printf("MQTT event unsubscribed\n");
    } else if(event_id == MQTT_EVENT_DATA) {
        printf("MQTT event data\n");
        // int dummy = 1;
        // TODO: Decoding
        // xQueueSend(g_mqtt_consume_queue, &dummy, portMAX_DELAY);
    } else if(event_id == MQTT_EVENT_ERROR) {
        printf("MQTT event error\n");
    }
}

int is_mqtt_connected(void) {
    return g_is_connected;
}

esp_err_t midi_mqtt_init(midi_mqtt_config_t *config) {
    esp_mqtt_client_config_t mqtt_config;
    memset(&mqtt_config, 0, sizeof(esp_mqtt_client_config_t));

    mqtt_config.broker.address.uri = config->host;
    mqtt_config.credentials.username = config->username;
    mqtt_config.credentials.authentication.password = config->passwd;

    esp_mqtt_client_handle_t handle = esp_mqtt_client_init(&mqtt_config);
    if (handle == NULL) {
        ESP_LOGE(MQTT_LOG_TAG, "Error initializing mqtt client");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL), MQTT_LOG_TAG, "Error registering mqtt event handler");
    ESP_RETURN_ON_ERROR(esp_mqtt_client_start(handle), MQTT_LOG_TAG, "Error starting mqtt client");

    g_mqtt_handle = handle;

    memcpy(g_mqtt_topic, config->topic, MQTT_TOPIC_LENGTH * sizeof(char));
    g_mqtt_topic[MQTT_TOPIC_LENGTH - 1] = '\0';

    g_mqtt_publish_queue = xQueueCreate(MQTT_PUBLISH_QUEUE_SIZE, sizeof(midi_mqtt_publish_data_t));
    if (g_mqtt_publish_queue == NULL) {
        ESP_LOGE(MQTT_LOG_TAG, "Error creating mqtt publish queue");
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t status = xTaskCreate(mqtt_publish_handler, MQTT_PUBLISH_TASK_NAME, 2048, NULL, tskIDLE_PRIORITY, &g_mqtt_publish_task);
    if (status != pdPASS) {
        ESP_LOGE(MQTT_LOG_TAG, "Error creating mqtt publish task");
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t midi_mqtt_deinit(void) {
    ESP_RETURN_ON_ERROR(esp_mqtt_client_stop(g_mqtt_handle), MQTT_LOG_TAG, "Error stopping mqtt client");
    ESP_RETURN_ON_ERROR(esp_mqtt_client_unregister_event(g_mqtt_handle, ESP_EVENT_ANY_ID, mqtt_event_handler), MQTT_LOG_TAG, "Error removing mqtt event handler");
    ESP_RETURN_ON_ERROR(esp_mqtt_client_destroy(g_mqtt_handle), MQTT_LOG_TAG, "Error destroying mqtt client");

    g_mqtt_handle = NULL;
    memset(g_mqtt_topic, 0, MQTT_TOPIC_LENGTH * sizeof(char));

    if (g_mqtt_publish_task != NULL) {
        vTaskDelete(g_mqtt_publish_task);
        g_mqtt_publish_task = NULL;
    }

    if (g_mqtt_publish_queue != NULL) {
        vQueueDelete(g_mqtt_publish_queue);
        g_mqtt_publish_queue = NULL;
    }

    return ESP_OK;
}

esp_err_t midi_mqtt_publish(midi_mqtt_publish_data_t *data) {
    return xQueueSend(g_mqtt_publish_queue, data, portMAX_DELAY);
}

