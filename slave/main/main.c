#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "hal/ledc_types.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "portmacro.h"
#include "esp_err.h"
#include "iot_servo.h"
#include "main.h"

#define QUEUE_SIZE 10
#define PAYLOAD_SIZE sizeof(Payload)
Payload payload;
static StaticQueue_t recv_queue;
static QueueHandle_t recv_handler;
static uint8_t recv_data[QUEUE_SIZE * PAYLOAD_SIZE]__attribute__((aligned(4)));

const int AIN1 = 5;
const int AIN2 = 17;
const int BIN1 = 16;
const int BIN2 = 2;
const int PWMA = 14;
const int PWMB = 15;
const int STBY = 18;
const int PITCH_PIN = 21;
const int YAW_PIN = 22; 
const int INDEXER_PIN = 4; 

void initializeNVS() 
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void initializeWifi()
{
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

void on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) 
{
    ESP_LOGI("SLAVE", "Message received from Master!");

    if (xQueueSend(recv_handler, &data, 0) != pdPASS) {
        ESP_LOGI("RECV DATA", "FAILED TO POST DATA TO CONSUMER");
    }
}

void handle_recv_data(void* params)
{
    Payload recv_payload;
    xQueueReceive(recv_handler, &recv_payload, portMAX_DELAY);

    ESP_LOGE("COMMANDS", "UP: %d, DOWN: %d, LEFT: %d, RIGHT: %d, INDEXER: %d", 
                                                            recv_payload.upState, 
                                                            recv_payload.downState, 
                                                            recv_payload.leftState, 
                                                            recv_payload.rightState, 
                                                            recv_payload.indexerState);
}
void app_main(void)
{
    initializeNVS();
    initializeWifi();
    esp_now_init();

    // configure all servos at once
    servo_config_t servo_cfg = {
        .max_angle = 180,
        .min_width_us = 500,
        .max_width_us = 2500,
        .freq = 50,
        .timer_number = LEDC_TIMER_0,
        .channels = {
            .servo_pin = {
                PITCH_PIN,
                YAW_PIN,
                INDEXER_PIN,
            },
            .ch = {
                LEDC_CHANNEL_0,
                LEDC_CHANNEL_1,
                LEDC_CHANNEL_2
            },
        },
        .channel_number = 3,
    };
    iot_servo_init(LEDC_HIGH_SPEED_MODE, &servo_cfg);

    recv_handler = xQueueCreateStatic(QUEUE_SIZE, 
                                        PAYLOAD_SIZE,
                                        recv_data,
                                        &recv_queue);
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    xTaskCreatePinnedToCore(handle_recv_data, 
                            "Receiving Data Task" ,
                            4096, 
                            NULL, 
                            4, 
                            NULL, 
                            1);
}
