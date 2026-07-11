#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef enum {
    COPET_BLE_OFF,
    COPET_BLE_STARTING,
    COPET_BLE_ADVERTISING,
    COPET_BLE_CONNECTED,
    COPET_BLE_ERROR,
} copet_ble_status_t;

esp_err_t copet_ble_init(void);
esp_err_t copet_ble_start(void);
void copet_ble_stop(void);
copet_ble_status_t copet_ble_get_status(void);

/* Returns true once for each newest message written to characteristic FFF1. */
bool copet_ble_take_message(char *buffer, size_t capacity);
