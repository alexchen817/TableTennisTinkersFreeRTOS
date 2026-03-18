
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "esp_interface.h"
#include "esp_now.h"
#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "hal/gpio_types.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_crc.h"
#include "portmacro.h"
#include "sdkconfig.h"
#include "main.h"
#include "driver/gpio.h"

static uint8_t slave_mac_addr[ESP_NOW_ETH_ALEN] = {0x94, 0xE6, 0x86, 0x3B, 0x5D, 0x9C};
static esp_now_peer_info_t peer;

static const int UP_BUTTON_PIN = 13;
static const int DOWN_BUTTON_PIN = 12;
static const int RIGHT_BUTTON_PIN = 14;
static const int LEFT_BUTTON_PIN = 27;
static const int INDEXER_BUTTON_PIN = 26;

#define QUEUE_SIZE 10
#define PAYLOAD_SIZE sizeof(Payload)
static Payload payload;
static StaticQueue_t static_queue;
static QueueHandle_t queue_handler;
static uint8_t queue_buffer[QUEUE_SIZE * sizeof(Payload)];

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

void esp_now_task(void* params) 
{
    // this task must never return or end!
    Payload recieved_payload;
    while (true) { 
        // block until a payload is pushed to queue
        if (xQueueReceive(queue_handler,&recieved_payload, portMAX_DELAY) == pdPASS) {
            esp_now_send(slave_mac_addr, (uint8_t *)&recieved_payload, sizeof(Payload));
        }
    }
}

esp_err_t initializeESPNOW()
{
    ESP_ERROR_CHECK(esp_now_init());

    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    peer.channel = CONFIG_ESPNOW_CHANNEL;
    peer.ifidx = ESP_IF_WIFI_STA;
    peer.encrypt = false;
    memcpy(peer.peer_addr, slave_mac_addr, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    xTaskCreatePinnedToCore(
    esp_now_task,
    "ESP_NOW_Task",    
    2048,        
    NULL,             
    5,              
    NULL,   
    1
    );

    return ESP_OK;
}

void button_state_task(void* params) 
{
    // this task must never return or end!
    while (true) {
        payload.upState = !gpio_get_level(UP_BUTTON_PIN);
        payload.downState = !gpio_get_level(DOWN_BUTTON_PIN);
        payload.leftState = !gpio_get_level(LEFT_BUTTON_PIN);
        payload.rightState = !gpio_get_level(RIGHT_BUTTON_PIN);
        payload.indexerState = !gpio_get_level(INDEXER_BUTTON_PIN);

        if (payload.upState && payload.downState) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        } else if (payload.rightState && payload.leftState) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        xQueueSend(queue_handler, &payload, 0);
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    initializeNVS();
    initializeWifi();
    initializeESPNOW();

    gpio_config_t buttonConfigs = {
        // activate all pins in one go
        .pin_bit_mask = ((1ULL << UP_BUTTON_PIN)
                        | (1ULL << DOWN_BUTTON_PIN)
                        | (1ULL << LEFT_BUTTON_PIN)
                        | (1ULL << RIGHT_BUTTON_PIN
                        | (1ULL << INDEXER_BUTTON_PIN))),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  
        .pull_down_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&buttonConfigs));

    queue_handler = xQueueCreateStatic(
                                       QUEUE_SIZE,
                                       PAYLOAD_SIZE,
                                       queue_buffer,
                                       &static_queue
                                    );

    xTaskCreatePinnedToCore(
        button_state_task,
        "Button State Task",
        2048, 
        NULL, 
        4, 
        NULL,
        1
    );
}
