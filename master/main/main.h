#pragma once
#include "esp_err.h"
#include <stdint.h>
#include "esp_now.h"

static uint8_t slave_mac_addr[ESP_NOW_ETH_ALEN] = {0x94, 0xE6, 0x86, 0x3B, 0x5D, 0x9C};

static esp_now_peer_info_t peer;

typedef struct {
    uint8_t up;
    uint8_t down;
    uint8_t left;
    uint8_t right;
} payload;

void initializeNVS();
void intializeWifi();
esp_err_t intializeESPNOW();