#pragma once
#include "esp_err.h"
#include <stdint.h>

typedef struct {
    uint8_t upState;
    uint8_t downState;
    uint8_t leftState;
    uint8_t rightState;
    uint8_t indexerState;
} __attribute__((packed)) Payload;

void initializeNVS();
void intializeWifi();