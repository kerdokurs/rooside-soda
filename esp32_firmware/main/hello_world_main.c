/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include <driver/gpio.h>

#include <stdlib.h>
#include <string.h>

#include "wifi.h"
#include "mqtt.h"

#include "sdkconfig.h"

static nvs_handle_t g_storage_handle;

#define STORAGE_NAMESPACE "storage"
#define WIFI_CREDENTIALS_KEY "wifi_credentials"

static volatile bool wifi_connected = false;

static esp_err_t init_storage() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &g_storage_handle);

    return err;
}

static void deinit_storage() {
    nvs_close(g_storage_handle);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {                                                                     
    if (event_id == WIFI_EVENT_STA_START) {                                                                                                                                
        printf("WIFI CONNECTING...\n");                                                                                                                                    
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {                                                                                                                     
        printf("WIFI CONNECTED...\n");                                                                                                                                     
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {                                                                                                                  
        printf("WIFI DISCONNECTED...\n");
        wifi_connected = false;
    } else if (event_id == IP_EVENT_STA_GOT_IP) {                                                                                                                          
        printf("WIFI GOT IP...\n");                                                                                                                                        
        wifi_connected = true;
    }                                                                                                                                                                      
} 

static QueueHandle_t g_gpio_event_queue;

static void IRAM_ATTR gpio_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(g_gpio_event_queue, &gpio_num, NULL);
}

#define TEAM0_GPIO_IN 21
#define TEAM1_GPIO_IN 22
#define GPIO_EVENT_QUEUE_SIZE 32

#define INFO_OUT_GPIO GPIO_NUM_23

esp_err_t input_init(void) {
    gpio_config_t gpio_cfg;
    
    uint64_t pin_mask = (1ULL << TEAM0_GPIO_IN) | (1ULL << TEAM1_GPIO_IN);

    gpio_cfg.pin_bit_mask = pin_mask;
    gpio_cfg.mode = GPIO_MODE_INPUT;
    gpio_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_cfg.intr_type = GPIO_INTR_POSEDGE;

    esp_err_t err = gpio_config(&gpio_cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_isr_handler_add(TEAM0_GPIO_IN, gpio_isr_handler, (void*)TEAM0_GPIO_IN);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_isr_handler_add(TEAM1_GPIO_IN, gpio_isr_handler, (void*)TEAM1_GPIO_IN);
    if (err != ESP_OK) {
        return err;
    }

    g_gpio_event_queue = xQueueCreate(GPIO_EVENT_QUEUE_SIZE, sizeof(gpio_num_t));

    gpio_reset_pin(INFO_OUT_GPIO);
    gpio_set_direction(INFO_OUT_GPIO, GPIO_MODE_OUTPUT);

    return ESP_OK;
}

esp_err_t input_deinit(void) {
    esp_err_t err = gpio_isr_handler_remove(TEAM0_GPIO_IN);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_isr_handler_remove(TEAM1_GPIO_IN);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

const char* ssid = "#Telia-F1C9A1";
const char* passwd = "wTNEYTYtefTYZPPH";

const char* mqtt_username = "test";
const char* mqtt_password = "test";
const char* mqtt_uri = "mqtt://34.88.20.179:1883";
const char* mqtt_topic = "button/pressed";

static int s_led_state = 0;

static void blink_led(void)
{
    gpio_set_level(INFO_OUT_GPIO, s_led_state);
}

void error_state(void) {
    for (;;) {
        s_led_state = !s_led_state;
        blink_led();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

#define TIMER_SEMAPHORE_TIMEOUT 1 / portTICK_PERIOD_MS
static SemaphoreHandle_t g_timer_semaphore;
static _Atomic int ticks = 0;

static QueueHandle_t g_restart_queue;

void main_gpio_handler(void *arg) {
    (void) arg;

    ESP_LOGI("GPIO", "Starting main gpio handler");

    for (;;) {
        int io_num;
        if (xQueueReceive(g_gpio_event_queue, &io_num, portMAX_DELAY)) {
            if (xSemaphoreTake(g_timer_semaphore, TIMER_SEMAPHORE_TIMEOUT) != pdTRUE) {
                ESP_LOGE("GPIO", "Failed to take semaphore");
                continue;
            }

            const int current_ticks = ticks;
            xSemaphoreGive(g_timer_semaphore);

            if (io_num == GPIO_NUM_20) {
                printf("restart pressed.\n");
                vTaskDelay(100 / portTICK_PERIOD_MS);
                xQueueSend(g_restart_queue, (void*)1, portMAX_DELAY);
                break;
            }
            int level = gpio_get_level(io_num);
            printf("GPIO[%d] intr, val: %d\n", io_num, level);

            if (level == 1) {
                int button = io_num == TEAM0_GPIO_IN ? 0 : 1;

                midi_mqtt_publish_data_t data;
                data.button = pvPortMalloc(sizeof(int));
                data.ticks = pvPortMalloc(sizeof(int));
                *data.button = button;
                *data.ticks = current_ticks;

                printf("Button: %d; Time: %d\n", *data.button, *data.ticks);

                midi_mqtt_publish(&data);
            }
        }
    }
}

void timer_task(void *arg) {
    (void) arg;

    // Tick every 10 ms
    const int time = 10 / portTICK_PERIOD_MS;
    for (;;) {
        if (xSemaphoreTake(g_timer_semaphore, TIMER_SEMAPHORE_TIMEOUT) != pdTRUE) {
            ESP_LOGE("TIMER", "Failed to take semaphore");
            continue;
        }

        ticks++;

        xSemaphoreGive(g_timer_semaphore);
        vTaskDelay(time);
    }
}

void app_main(void) {
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    nvs_flash_init();

    esp_err_t err = input_init();
    if (err != ESP_OK) {
        printf("Failed to initialize input: %d\n", err);
        error_state();
    }

    wifi_credentials_t wifi_credentials;
    memset(&wifi_credentials, 0, sizeof(wifi_credentials_t));
    strcpy(wifi_credentials.ssid, ssid);
    strcpy(wifi_credentials.passwd, passwd);
    wifi_credentials.wifi_event_handler = wifi_event_handler;

    ESP_LOGI("WIFI", "Connecting to wifi");

    err = midi_wifi_init(&wifi_credentials);
    if (err != ESP_OK) {
        printf("Failed to initialize wifi: %d\n", err);
        error_state();
    }

    ESP_LOGI("WIFI", "Waiting for wifi connection");

    while (!wifi_connected) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        s_led_state = !s_led_state;
        blink_led();
    }

    ESP_LOGI("WIFI", "Connected to wifi");

    midi_mqtt_config_t mqtt_config;
    memset(&mqtt_config, 0, sizeof(midi_mqtt_config_t));
    strcpy(mqtt_config.host, mqtt_uri);
    strcpy(mqtt_config.username, mqtt_username);
    strcpy(mqtt_config.passwd, mqtt_password);
    strcpy(mqtt_config.topic, mqtt_topic);

    ESP_LOGI("MQTT", "Connecting to mqtt");
    
    err = midi_mqtt_init(&mqtt_config);
    if (err != ESP_OK) {
        printf("Failed to initialize mqtt: %d\n", err);
        error_state();
    }

    ESP_LOGI("MQTT", "Waiting for mqtt connection");

    while (!is_mqtt_connected()) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        s_led_state = !s_led_state;
        blink_led();
    }

    ESP_LOGI("MQTT", "Connected to mqtt");

    s_led_state = 1;
    blink_led();

    g_timer_semaphore = xSemaphoreCreateMutex();

    g_restart_queue = xQueueCreate(1, sizeof(bool));

    TaskHandle_t gpio_task_handle;
    xTaskCreate(main_gpio_handler, "main_gpio_handler", 2048, NULL, 10, &gpio_task_handle);

    TaskHandle_t timer_task_handle;
    xTaskCreate(timer_task, "timer_task", 2048, NULL, 10, &timer_task_handle);

    for (;;) {
        int restart;
        if (xQueueReceive(g_restart_queue, &restart, portMAX_DELAY)) {
            ESP_LOGI("MAIN", "Restarting...\n");
            break;
        }
    }

    err = input_deinit();
    if (err != ESP_OK) {
        printf("Failed to deinitialize input: %d\n", err);
    }
    ESP_ERROR_CHECK(midi_mqtt_deinit());
    ESP_ERROR_CHECK(midi_wifi_deinit());
    nvs_flash_deinit();
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}
