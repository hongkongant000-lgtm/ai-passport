// Connectable BLE peripheral for phone pairing / status. Separate from BLUFI mode.
#include "app_ble.h"
#include "app_config.h"
#include "app_radio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "app_ble";

#define BLE_STOP_TIMEOUT_MS 2000
#define STATUS_MAX 48

static SemaphoreHandle_t s_host_stopped;
static TaskHandle_t s_host_task;
static volatile app_ble_state_t s_state = APP_BLE_OFF;
static volatile esp_err_t s_error;
static uint8_t s_addr_type;
static bool s_initialized;
static volatile bool s_start_requested;
static bool s_stop_in_progress;
static bool s_host_done;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_status_val_handle;
static char s_status_text[STATUS_MAX] = "idle";
static app_ble_ctrl_cb_t s_ctrl_cb;
static void *s_ctrl_user;

static int gap_event(struct ble_gap_event *event, void *arg);
static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg);

static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(APP_GATT_SVC_UUID128);
static const ble_uuid128_t s_status_uuid =
    BLE_UUID128_INIT(APP_GATT_CHR_STATUS_UUID128);
static const ble_uuid128_t s_ctrl_uuid =
    BLE_UUID128_INIT(APP_GATT_CHR_CTRL_UUID128);

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_status_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_status_val_handle,
            },
            {
                .uuid = &s_ctrl_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 },
        },
    },
    { 0 },
};

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, s_status_text, strlen(s_status_text));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t opcode = 0;
        if (OS_MBUF_PKTLEN(ctxt->om) >= 1) {
            int rc = ble_hs_mbuf_to_flat(ctxt->om, &opcode, 1, NULL);
            if (rc != 0) return BLE_ATT_ERR_UNLIKELY;
            if (s_ctrl_cb) s_ctrl_cb(opcode, s_ctrl_user);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int advertise(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)APP_DEVICE_NAME;
    fields.name_len = strlen(APP_DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.uuids128 = &s_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc == 0) s_state = APP_BLE_ADVERTISING;
    return rc;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_state = APP_BLE_CONNECTED;
            ESP_LOGI(TAG, "phone connected");
        } else if (s_start_requested) {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "phone disconnected, reason=%d", event->disconnect.reason);
        if (s_start_requested) advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_start_requested && s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            int rc = advertise();
            if (rc != 0) {
                s_error = rc;
                s_state = APP_BLE_FAILED;
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

static void on_reset(int reason)
{
    s_error = reason;
    s_state = APP_BLE_FAILED;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0 && s_start_requested) rc = advertise();
    if (rc != 0) {
        s_error = rc;
        s_state = APP_BLE_FAILED;
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    xSemaphoreGive(s_host_stopped);
    for (;;) vTaskSuspend(NULL);
}

esp_err_t app_ble_start(void)
{
    if (s_initialized) {
        s_error = ESP_ERR_INVALID_STATE;
        s_state = APP_BLE_FAILED;
        return ESP_ERR_INVALID_STATE;
    }

    s_state = APP_BLE_STARTING;
    s_stop_in_progress = false;
    s_host_done = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_start_requested = true;

    esp_err_t err = app_radio_nvs_prepare();
    if (err != ESP_OK) {
        s_error = err;
        s_state = APP_BLE_FAILED;
        s_start_requested = false;
        return err;
    }

    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) {
        s_error = ESP_ERR_NO_MEM;
        s_state = APP_BLE_FAILED;
        s_start_requested = false;
        return ESP_ERR_NO_MEM;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
        s_error = err;
        s_state = APP_BLE_FAILED;
        s_start_requested = false;
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc == 0) rc = ble_svc_gap_device_name_set(APP_DEVICE_NAME);
    if (rc != 0) {
        nimble_port_deinit();
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
        s_error = ESP_FAIL;
        s_state = APP_BLE_FAILED;
        s_start_requested = false;
        return ESP_FAIL;
    }

    if (xTaskCreate(host_task, "nimble_host", 4096, NULL, 5, &s_host_task) != pdPASS) {
        nimble_port_deinit();
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
        s_host_task = NULL;
        s_error = ESP_ERR_NO_MEM;
        s_state = APP_BLE_FAILED;
        s_start_requested = false;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t app_ble_stop(void)
{
    if (!s_initialized) return ESP_OK;
    if (s_stop_in_progress) return ESP_ERR_INVALID_STATE;
    s_stop_in_progress = true;
    s_start_requested = false;

    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    ble_gap_adv_stop();

    int rc = nimble_port_stop();
    if (rc != 0) {
        s_stop_in_progress = false;
        return ESP_FAIL;
    }
    if (xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(BLE_STOP_TIMEOUT_MS)) != pdTRUE) {
        s_stop_in_progress = false;
        return ESP_ERR_TIMEOUT;
    }
    nimble_port_deinit();
    if (s_host_task) {
        vTaskDelete(s_host_task);
        s_host_task = NULL;
    }
    if (s_host_stopped) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
    s_initialized = false;
    s_stop_in_progress = false;
    s_state = APP_BLE_OFF;
    return ESP_OK;
}

app_ble_state_t app_ble_state(void) { return s_state; }
esp_err_t app_ble_last_error(void) { return s_error; }
bool app_ble_is_connected(void) { return s_conn_handle != BLE_HS_CONN_HANDLE_NONE; }

void app_ble_set_status_text(const char *text)
{
    if (!text) text = "";
    strncpy(s_status_text, text, sizeof(s_status_text) - 1);
    s_status_text[sizeof(s_status_text) - 1] = '\0';
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_status_val_handle == 0) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(s_status_text, strlen(s_status_text));
    if (om) ble_gatts_notify_custom(s_conn_handle, s_status_val_handle, om);
}

void app_ble_set_ctrl_callback(app_ble_ctrl_cb_t cb, void *user)
{
    s_ctrl_cb = cb;
    s_ctrl_user = user;
}
