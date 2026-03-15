#pragma once
#include "esp_err.h"
#include <stdint.h>
#include "esp_now.h"

static uint8_t slave_mac_addr[ESP_NOW_ETH_ALEN] = {0x94, 0xE6, 0x86, 0x3B, 0x5D, 0x9C};
void initializeNVS();
void intializeWifi();
esp_err_t intializeESPNOW();