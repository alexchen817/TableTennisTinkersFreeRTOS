
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "esp_interface.h"
#include "esp_now.h"
#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_crc.h"
#include "sdkconfig.h"
#include "main.h"

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
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

esp_err_t initializeESPNOW()
{
    esp_now_init();
    esp_now_set_pmk((uint8_t*)CONFIG_ESPNOW_PMK);

    // add peer information
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE("master", "MALLOC PEER INFO FAILURE");
        return ESP_FAIL;
    }

    // add slave
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESP_IF_WIFI_STA;
    peer->encrypt = false;
    memcpy(peer->peer_addr, slave_mac_addr, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);


    return ESP_OK;
}

void app_main(void)
{
    initializeNVS();
    initializeWifi();
    initializeESPNOW();
}
