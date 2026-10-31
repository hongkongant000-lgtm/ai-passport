#include "app_wifi.h"
#include "app_radio.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "lwip/ip4_addr.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "app_wifi";

static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static volatile app_wifi_state_t s_state = APP_WIFI_OFF;
static volatile esp_err_t s_error;
static char s_ip[16];
static char s_ssid[33];
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_wifi_handler_registered;
static bool s_ip_handler_registered;
static bool s_got_ip;
static bool s_reconnect_after_disconnect;
static wifi_config_t s_sta_config;

static void copy_ssid_from_config(void)
{
    size_t n = strnlen((const char *)s_sta_config.sta.ssid, sizeof(s_sta_config.sta.ssid));
    if (n >= sizeof(s_ssid)) n = sizeof(s_ssid) - 1;
    memcpy(s_ssid, s_sta_config.sta.ssid, n);
    s_ssid[n] = '\0';
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_get_config(WIFI_IF_STA, &s_sta_config);
        copy_ssid_from_config();
        if (s_sta_config.sta.ssid[0] != '\0') {
            s_state = APP_WIFI_CONNECTING;
            esp_wifi_connect();
        } else {
            s_state = APP_WIFI_DISCONNECTED;
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "disconnected, reason=%u", event->reason);
        if (s_reconnect_after_disconnect) {
            s_reconnect_after_disconnect = false;
            esp_wifi_connect();
            return;
        }
        s_got_ip = false;
        s_ip[0] = '\0';
        s_state = APP_WIFI_DISCONNECTED;
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;
    ip_event_got_ip_t *event = data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
    s_got_ip = true;
    s_state = APP_WIFI_CONNECTED;
    ESP_LOGI(TAG, "got IP %s", s_ip);
}

esp_err_t app_wifi_start(void)
{
    if (s_sta_netif || s_wifi_initialized) return ESP_ERR_INVALID_STATE;
    s_state = APP_WIFI_STARTING;
    s_ip[0] = '\0';
    s_ssid[0] = '\0';
    s_got_ip = false;

    esp_err_t err = app_radio_nvs_prepare();
    if (err != ESP_OK) goto fail;
    err = app_radio_network_prepare();
    if (err != ESP_OK) goto fail;

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    s_sta_netif = esp_netif_new(&netif_cfg);
    if (!s_sta_netif) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    err = esp_netif_attach_wifi_station(s_sta_netif);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_default_wifi_sta_handlers();
    if (err != ESP_OK) goto fail;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) goto fail;
    s_wifi_initialized = true;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) goto fail;
    s_wifi_handler_registered = true;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              ip_event, NULL, &s_ip_handler);
    if (err != ESP_OK) goto fail;
    s_ip_handler_registered = true;

    // Persist credentials across reboot so BLUFI / phone setup sticks.
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_wifi_started = true;
    return ESP_OK;

fail:
    s_error = err;
    s_state = APP_WIFI_FAILED;
    app_wifi_stop();
    ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
    return err;
}

void app_wifi_stop(void)
{
    s_got_ip = false;
    s_ip[0] = '\0';
    if (s_wifi_started) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_ip_handler_registered) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        s_ip_handler_registered = false;
    }
    if (s_wifi_handler_registered) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        s_wifi_handler_registered = false;
    }
    if (s_wifi_initialized) {
        esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_state != APP_WIFI_FAILED) s_state = APP_WIFI_OFF;
}

app_wifi_state_t app_wifi_state(void) { return s_state; }
esp_err_t app_wifi_last_error(void) { return s_error; }
bool app_wifi_got_ip(void) { return s_got_ip; }

void app_wifi_get_ip(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    snprintf(buf, len, "%s", s_ip);
}

void app_wifi_get_ssid(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    snprintf(buf, len, "%s", s_ssid);
}

esp_err_t app_wifi_reconnect(void)
{
    if (!s_wifi_started) return ESP_ERR_INVALID_STATE;
    bool was = s_got_ip;
    s_got_ip = false;
    s_ip[0] = '\0';
    s_state = APP_WIFI_CONNECTING;
    if (was) {
        s_reconnect_after_disconnect = true;
        if (esp_wifi_disconnect() == ESP_OK) return ESP_OK;
        s_reconnect_after_disconnect = false;
    }
    return esp_wifi_connect();
}

esp_err_t app_wifi_clear_credentials(void)
{
    if (!s_wifi_started) return ESP_ERR_INVALID_STATE;
    wifi_config_t empty = { 0 };
    s_reconnect_after_disconnect = false;
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &empty);
    memset(&s_sta_config, 0, sizeof(s_sta_config));
    s_ssid[0] = '\0';
    s_got_ip = false;
    s_ip[0] = '\0';
    s_state = APP_WIFI_DISCONNECTED;
    return err;
}

esp_err_t app_wifi_apply_sta_config(const void *wifi_config)
{
    if (!wifi_config || !s_wifi_started) return ESP_ERR_INVALID_STATE;
    memcpy(&s_sta_config, wifi_config, sizeof(s_sta_config));
    copy_ssid_from_config();
    return esp_wifi_set_config(WIFI_IF_STA, &s_sta_config);
}

void *app_wifi_sta_netif(void)
{
    return s_sta_netif;
}
