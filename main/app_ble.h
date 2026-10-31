#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_BLE_OFF = 0,
    APP_BLE_STARTING,
    APP_BLE_ADVERTISING,
    APP_BLE_CONNECTED,
    APP_BLE_FAILED,
} app_ble_state_t;

// Connectable NimBLE peripheral with a small custom GATT status/control service.
esp_err_t app_ble_start(void);
esp_err_t app_ble_stop(void);
app_ble_state_t app_ble_state(void);
esp_err_t app_ble_last_error(void);
bool app_ble_is_connected(void);

// Pack a short status string into the GATT status characteristic (best-effort notify).
void app_ble_set_status_text(const char *text);

// Optional control callback from phone writes (runs on NimBLE host task — keep light).
typedef void (*app_ble_ctrl_cb_t)(uint8_t opcode, void *user);
void app_ble_set_ctrl_callback(app_ble_ctrl_cb_t cb, void *user);
