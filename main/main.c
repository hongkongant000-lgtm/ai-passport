// main/main.c — Beatbox Passport offline studio entry.
// Redesigned LVGL UI; reuses BSP only (no demo menu / ui_pixel shell).
#include "beatbox_audio.h"
#include "beatbox_engine.h"
#include "beatbox_ui.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "key_guard.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "beatbox";

#define INPUT_QUEUE_DEPTH 8
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static beatbox_app_t s_app;
static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;
static key_guard_t s_key_guard;

static void process_input(const input_event_t *input)
{
    if (!bsp_lvgl_lock(500)) {
        return;
    }
    beatbox_ui_on_key(&s_app, input->btn, input->event);
    bsp_lvgl_unlock();
}

static void input_task(void *arg)
{
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            process_input(&input);
        }
    }
}

static esp_err_t input_dispatch_init(void)
{
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(input_task, "bb_input", 4096, NULL, 5, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "wake key released; input armed");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) {
        return;
    }
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Beatbox Passport starting (offline milestone 1)");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "key wake: suppress until release");
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed. check SPI(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    memset(&s_app, 0, sizeof(s_app));
    strncpy(s_app.display_name, "Guest", sizeof(s_app.display_name) - 1);
    beatbox_pattern_for_style(BEATBOX_STYLE_TRAP, &s_app.pattern);
    s_app.bpm_from_stub = true;
    s_app.battery_ok = (bsp_battery_init() == ESP_OK);

    esp_err_t audio_err = beatbox_audio_start();
    s_app.audio_ok = (audio_err == ESP_OK);
    if (audio_err != ESP_OK) {
        ESP_LOGW(TAG, "audio worker failed: %s (UI still runs)", esp_err_to_name(audio_err));
    } else {
        beatbox_audio_set_pattern(&s_app.pattern);
    }

    esp_err_t input_err = input_dispatch_init();
    esp_err_t button_err = input_err == ESP_OK ? bsp_button_init(on_key, NULL)
                                               : ESP_ERR_INVALID_STATE;
    if (input_err != ESP_OK) {
        ESP_LOGE(TAG, "input task failed: %s", esp_err_to_name(input_err));
    } else if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s", esp_err_to_name(button_err));
    } else if (s_key_guard.armed) {
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) {
            ESP_LOGI(TAG, "wake key already released; disarm early");
        }
    }

    if (bsp_lvgl_lock(1000)) {
        beatbox_ui_init(&s_app);
        bsp_lvgl_unlock();
        s_input_ready = (input_err == ESP_OK && button_err == ESP_OK);
    }

    ESP_LOGI(TAG, "ready: battery=%d audio=%d input=%d",
             s_app.battery_ok, s_app.audio_ok, s_input_ready);
}
