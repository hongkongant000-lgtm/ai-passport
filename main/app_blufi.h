#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    APP_BLUFI_OFF = 0,
    APP_BLUFI_STARTING,
    APP_BLUFI_ADVERTISING,
    APP_BLUFI_BLE_CONNECTED,
    APP_BLUFI_WIFI_CONNECTING,
    APP_BLUFI_WIFI_CONNECTED,
    APP_BLUFI_FAILED,
} app_blufi_state_t;

// Starts BLUFI provisioning. Stops the custom app_ble peripheral first (same NimBLE host).
esp_err_t app_blufi_start(void);
void app_blufi_stop(void);
bool app_blufi_is_active(void);
app_blufi_state_t app_blufi_state(void);
esp_err_t app_blufi_last_error(void);
