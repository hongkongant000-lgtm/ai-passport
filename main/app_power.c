#include "app_power.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_power";

#define DEEP_SLEEP_TIME_US (5ULL * 1000ULL * 1000ULL)

static void sleep_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "preparing deep sleep");

    int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) {
        ESP_LOGW(TAG, "key held (%d mV); abort deep sleep", mv);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_sleep_enable_timer_wakeup(DEEP_SLEEP_TIME_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "timer wake arm failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    int level = 0;
    err = bsp_button_prepare_deep_sleep(&level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "button handoff failed: %s", esp_err_to_name(err));
        esp_restart();
    }
    (void)esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO,
                                            ESP_GPIO_WAKEUP_GPIO_LOW);

    // Terminal order required by deep-sleep contract tests.
    bsp_battery_sleep();
    bsp_audio_sleep();
    bsp_audio_prepare_deep_sleep();
    bsp_i2c_prepare_deep_sleep();

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "cannot stop LVGL before panel sleep; restarting");
        esp_restart();
    }
    bsp_display_prepare_deep_sleep();
    esp_deep_sleep_start();

    ESP_LOGE(TAG, "esp_deep_sleep_start returned unexpectedly");
    esp_restart();
}

esp_err_t app_power_enter_deep_sleep(void)
{
    if (xTaskCreate(sleep_task, "app_sleep", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
