// BLUFI Wi-Fi provisioning adapted from upstream demo/blufi-provisioning patterns.
// Owns the NimBLE host while active; stops app_ble first to avoid dual-host conflicts.
#include "app_blufi.h"
#include "app_ble.h"
#include "app_blufi_security.h"
#include "app_config.h"
#include "app_radio.h"
#include "app_wifi.h"

#include "esp_blufi.h"
#include "esp_blufi_api.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>

static const char *TAG = "app_blufi";

#define BLUFI_AP_LIST_COUNT 16

static volatile app_blufi_state_t s_state = APP_BLUFI_OFF;
static volatile esp_err_t s_error;
static wifi_config_t s_sta_config;
static bool s_host_initialized;
static bool s_host_running;
static bool s_gatt_initialized;
static bool s_btc_initialized;
static bool s_profile_initialized;
static bool s_ble_connected;
static bool s_wifi_connecting;
static bool s_active;
static bool s_owned_wifi_start;
static SemaphoreHandle_t s_host_stopped;
static esp_event_handler_instance_t s_scan_handler;
static bool s_scan_handler_registered;

static void send_wifi_report(esp_blufi_sta_conn_state_t state)
{
    wifi_mode_t mode = WIFI_MODE_STA;
    esp_wifi_get_mode(&mode);
    esp_blufi_extra_info_t info = { 0 };
    size_t ssid_len = strnlen((const char *)s_sta_config.sta.ssid,
                              sizeof(s_sta_config.sta.ssid));
    if (ssid_len > 0) {
        info.sta_ssid = s_sta_config.sta.ssid;
        info.sta_ssid_len = ssid_len;
    }
    esp_blufi_send_wifi_conn_report(mode, state, 0, &info);
}

static void send_wifi_list(void)
{
    uint16_t count = BLUFI_AP_LIST_COUNT;
    wifi_ap_record_t records[BLUFI_AP_LIST_COUNT] = { 0 };
    esp_blufi_ap_record_t list[BLUFI_AP_LIST_COUNT] = { 0 };
    esp_err_t err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        return;
    }
    for (uint16_t i = 0; i < count; i++) {
        list[i].rssi = records[i].rssi;
        memcpy(list[i].ssid, records[i].ssid, sizeof(list[i].ssid));
    }
    if (s_ble_connected) esp_blufi_send_wifi_list(count, list);
}

static void request_wifi_connect(void)
{
    s_wifi_connecting = true;
    s_state = APP_BLUFI_WIFI_CONNECTING;
    esp_err_t err = app_wifi_reconnect();
    if (err != ESP_OK) {
        s_error = err;
        s_wifi_connecting = false;
        s_state = APP_BLUFI_FAILED;
    }
}

static void on_scan_done(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    if (s_active) send_wifi_list();
}

static void blufi_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset: %d", reason);
    s_error = ESP_FAIL;
    s_state = APP_BLUFI_FAILED;
}

static void blufi_sync(void)
{
    int rc = esp_blufi_profile_init();
    if (rc == 0) {
        s_profile_initialized = true;
    } else {
        s_error = rc;
        s_state = APP_BLUFI_FAILED;
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    if (s_host_stopped) xSemaphoreGive(s_host_stopped);
    nimble_port_freertos_deinit();
}

static void blufi_event(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        esp_blufi_adv_start_with_name(APP_BLUFI_DEVICE_NAME);
        if (app_wifi_got_ip()) {
            s_state = APP_BLUFI_WIFI_CONNECTED;
        } else if (s_wifi_connecting) {
            s_state = APP_BLUFI_WIFI_CONNECTING;
        } else {
            s_state = APP_BLUFI_ADVERTISING;
        }
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        s_ble_connected = true;
        esp_blufi_adv_stop();
        if (app_blufi_security_init() != 0) {
            s_error = ESP_ERR_NO_MEM;
            s_state = APP_BLUFI_FAILED;
        } else if (!app_wifi_got_ip()) {
            s_state = APP_BLUFI_BLE_CONNECTED;
        }
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        s_ble_connected = false;
        app_blufi_security_deinit();
        esp_blufi_adv_start_with_name(APP_BLUFI_DEVICE_NAME);
        s_state = app_wifi_got_ip() ? APP_BLUFI_WIFI_CONNECTED : APP_BLUFI_ADVERTISING;
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        esp_wifi_set_mode(WIFI_MODE_STA);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        memcpy(s_sta_config.sta.bssid, param->sta_bssid.bssid, 6);
        s_sta_config.sta.bssid_set = true;
        app_wifi_apply_sta_config(&s_sta_config);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        if (param->sta_ssid.ssid_len >= sizeof(s_sta_config.sta.ssid)) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            break;
        }
        memset(s_sta_config.sta.ssid, 0, sizeof(s_sta_config.sta.ssid));
        memset(s_sta_config.sta.password, 0, sizeof(s_sta_config.sta.password));
        memset(s_sta_config.sta.bssid, 0, sizeof(s_sta_config.sta.bssid));
        s_sta_config.sta.bssid_set = false;
        s_sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        memcpy(s_sta_config.sta.ssid, param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        app_wifi_apply_sta_config(&s_sta_config);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        if (param->sta_passwd.passwd_len >= sizeof(s_sta_config.sta.password)) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            break;
        }
        memset(s_sta_config.sta.password, 0, sizeof(s_sta_config.sta.password));
        memcpy(s_sta_config.sta.password, param->sta_passwd.passwd,
               param->sta_passwd.passwd_len);
        app_wifi_apply_sta_config(&s_sta_config);
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        request_wifi_connect();
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS:
        if (app_wifi_got_ip()) {
            send_wifi_report(ESP_BLUFI_STA_CONN_SUCCESS);
            s_state = APP_BLUFI_WIFI_CONNECTED;
        } else if (s_wifi_connecting || app_wifi_state() == APP_WIFI_CONNECTING) {
            send_wifi_report(ESP_BLUFI_STA_CONNECTING);
            s_state = APP_BLUFI_WIFI_CONNECTING;
        } else {
            send_wifi_report(ESP_BLUFI_STA_CONN_FAIL);
        }
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST: {
        wifi_scan_config_t config = { 0 };
        if (esp_wifi_scan_start(&config, false) != ESP_OK) {
            esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        }
        break;
    }
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        esp_blufi_disconnect();
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        esp_blufi_send_error_info(param->report_error.state);
        break;
    default:
        break;
    }
}

static esp_blufi_callbacks_t s_callbacks = {
    .event_cb = blufi_event,
    .negotiate_data_handler = app_blufi_negotiate,
    .encrypt_func = app_blufi_encrypt,
    .decrypt_func = app_blufi_decrypt,
    .checksum_func = app_blufi_checksum,
};

static esp_err_t host_start(void)
{
    esp_err_t err = esp_blufi_register_callbacks(&s_callbacks);
    if (err != ESP_OK) return err;
    err = nimble_port_init();
    if (err != ESP_OK) return err;
    s_host_initialized = true;
    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) return ESP_ERR_NO_MEM;
    ble_hs_cfg.reset_cb = blufi_reset;
    ble_hs_cfg.sync_cb = blufi_sync;
    ble_hs_cfg.gatts_register_cb = esp_blufi_gatt_svr_register_cb;
    int rc = esp_blufi_gatt_svr_init();
    if (rc != 0) return ESP_FAIL;
    s_gatt_initialized = true;
    rc = ble_svc_gap_device_name_set(APP_BLUFI_DEVICE_NAME);
    if (rc != 0) return ESP_FAIL;
    esp_blufi_btc_init();
    s_btc_initialized = true;
    err = esp_nimble_enable(host_task);
    if (err == ESP_OK) s_host_running = true;
    return err;
}

esp_err_t app_blufi_start(void)
{
    if (s_active) return ESP_ERR_INVALID_STATE;
    s_state = APP_BLUFI_STARTING;
    s_error = ESP_OK;
    s_wifi_connecting = false;
    s_ble_connected = false;
    s_owned_wifi_start = false;
    memset(&s_sta_config, 0, sizeof(s_sta_config));

    // Custom BLE and BLUFI cannot share one NimBLE host in this milestone.
    if (app_ble_state() != APP_BLE_OFF) {
        esp_err_t stop_err = app_ble_stop();
        if (stop_err != ESP_OK) {
            s_error = stop_err;
            s_state = APP_BLUFI_FAILED;
            return stop_err;
        }
    }

    esp_err_t err = app_radio_nvs_prepare();
    if (err != ESP_OK) goto fail;
    err = app_radio_network_prepare();
    if (err != ESP_OK) goto fail;

    if (app_wifi_state() == APP_WIFI_OFF || app_wifi_state() == APP_WIFI_FAILED) {
        err = app_wifi_start();
        if (err != ESP_OK) goto fail;
        s_owned_wifi_start = true;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                              on_scan_done, NULL, &s_scan_handler);
    if (err != ESP_OK) goto fail;
    s_scan_handler_registered = true;

    err = host_start();
    if (err != ESP_OK) goto fail;

    s_active = true;
    return ESP_OK;

fail:
    s_error = err;
    s_state = APP_BLUFI_FAILED;
    app_blufi_stop();
    ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
    return err;
}

void app_blufi_stop(void)
{
    s_ble_connected = false;
    app_blufi_security_deinit();
    if (s_host_initialized) {
        if (s_profile_initialized) esp_blufi_adv_stop();
        if (s_gatt_initialized) {
            esp_blufi_gatt_svr_deinit();
            s_gatt_initialized = false;
        }
        bool host_stopped = !s_host_running;
        if (s_host_running) {
            int rc = nimble_port_stop();
            if (rc == 0) {
                xSemaphoreTake(s_host_stopped, portMAX_DELAY);
                host_stopped = true;
            } else {
                ESP_LOGE(TAG, "nimble_port_stop failed: %d", rc);
            }
        }
        if (host_stopped) nimble_port_deinit();
        s_host_running = false;
        if (s_profile_initialized) {
            esp_blufi_profile_deinit();
            s_profile_initialized = false;
        }
        if (s_btc_initialized) {
            esp_blufi_btc_deinit();
            s_btc_initialized = false;
        }
        s_host_initialized = false;
    }
    if (s_host_stopped) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
    if (s_scan_handler_registered) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                              s_scan_handler);
        s_scan_handler_registered = false;
    }
    s_active = false;
    if (s_state != APP_BLUFI_FAILED) s_state = APP_BLUFI_OFF;
}

bool app_blufi_is_active(void) { return s_active; }
app_blufi_state_t app_blufi_state(void) { return s_state; }
esp_err_t app_blufi_last_error(void) { return s_error; }
