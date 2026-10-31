// Passport translator / speaker application entry (derivative UI — not the demo menu).
#include "app_ble.h"
#include "app_cloud.h"
#include "app_config.h"
#include "app_ui.h"
#include "app_wifi.h"
#include "key_guard.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_spiffs.h"
#include "esp_timer.h"

static const char *TAG = "main";

#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;
static volatile bool s_input_ready;

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    // Consume the guard before the readiness check: a release can arrive after the
    // post-init voltage sample but before input dispatch is ready, and dropping it
    // here would leave the guard armed to swallow the next real press.
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "wake key released; input restored");
        }
        return;
    }
    if (!s_input_ready) return;
    app_ui_on_key(btn, ev);
}

static void storage_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = APP_STORAGE_BASE_PATH,
        .partition_label = APP_STORAGE_PARTITION,
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s (archive later)", esp_err_to_name(err));
    } else {
        size_t total = 0, used = 0;
        if (esp_spiffs_info(APP_STORAGE_PARTITION, &total, &used) == ESP_OK) {
            ESP_LOGI(TAG, "storage %s: %u / %u bytes", APP_STORAGE_BASE_PATH,
                     (unsigned)used, (unsigned)total);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Passport translator starting (%s)", APP_DEVICE_NAME);
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & ~(1u << ESP_SLEEP_WAKEUP_UNDEFINED)) {
        ESP_LOGI(TAG, "wake cause bitmap: 0x%02x", (unsigned)wake_causes);
    }
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "GPIO wake: suppressing keys until release");
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    bool audio_ok = bsp_audio_init() == ESP_OK;
    bool battery_ok = bsp_battery_init() == ESP_OK;
    (void)app_cloud_init();
    storage_init();

    esp_err_t ui_err = app_ui_start();
    if (ui_err != ESP_OK) {
        ESP_LOGE(TAG, "UI start failed: %s", esp_err_to_name(ui_err));
        return;
    }

    esp_err_t button_err = bsp_button_init(on_key, NULL);
    if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s", esp_err_to_name(button_err));
    } else if (s_key_guard.armed) {
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) {
            ESP_LOGI(TAG, "wake key already released before button driver");
        }
    }
    s_input_ready = (button_err == ESP_OK);

    // Connectivity: Wi-Fi STA + connectable BLE peripheral.
    esp_err_t wifi_err = app_wifi_start();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi start: %s", esp_err_to_name(wifi_err));
    }
    esp_err_t ble_err = app_ble_start();
    if (ble_err != ESP_OK) {
        ESP_LOGW(TAG, "BLE start: %s", esp_err_to_name(ble_err));
    }
    app_ble_set_status_text("idle");

    ESP_LOGI(TAG, "ready audio=%d battery=%d wifi=%s ble=%s",
             audio_ok, battery_ok,
             esp_err_to_name(wifi_err), esp_err_to_name(ble_err));
}
