#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    APP_WIFI_OFF = 0,
    APP_WIFI_STARTING,
    APP_WIFI_DISCONNECTED,
    APP_WIFI_CONNECTING,
    APP_WIFI_CONNECTED,
    APP_WIFI_FAILED,
} app_wifi_state_t;

esp_err_t app_wifi_start(void);
void app_wifi_stop(void);
app_wifi_state_t app_wifi_state(void);
esp_err_t app_wifi_last_error(void);
bool app_wifi_got_ip(void);
void app_wifi_get_ip(char *buf, size_t len);
void app_wifi_get_ssid(char *buf, size_t len);
esp_err_t app_wifi_reconnect(void);
esp_err_t app_wifi_clear_credentials(void);

// Used by BLUFI path after credentials are written.
esp_err_t app_wifi_apply_sta_config(const void *wifi_config);
void *app_wifi_sta_netif(void);
