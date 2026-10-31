#pragma once

#include "esp_err.h"

// Shared NVS / netif / default event loop for Wi-Fi and NimBLE.
// Never erase NVS on init failure — user credentials may already be present.
esp_err_t app_radio_nvs_prepare(void);
esp_err_t app_radio_network_prepare(void);
