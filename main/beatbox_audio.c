// main/beatbox_audio.c — streaming I2S loop; keeps chunk buffer small (no PSRAM).
#include "beatbox_audio.h"

#include "bsp_audio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "beatbox_audio";

#define CHUNK_SAMPLES 256
#define AUDIO_STOP_TIMEOUT_MS 2000
#define AUDIO_TASK_STACK 4096

typedef enum {
    AUDIO_CMD_NONE = 0,
    AUDIO_CMD_APPLY = 1,
    AUDIO_CMD_STOP = 2,
} audio_cmd_t;

static TaskHandle_t s_task;
static SemaphoreHandle_t s_stopped;
static SemaphoreHandle_t s_lock;
static volatile bool s_cancel;
static volatile bool s_playing;
static beatbox_voice_t s_voice;
static beatbox_pattern_t s_pattern;
static bool s_pattern_ready;
static bool s_audio_ok;

static void apply_locked_pattern(void)
{
    if (!s_pattern_ready) {
        return;
    }
    beatbox_voice_set_pattern(&s_voice, &s_pattern);
}

static void audio_task(void *arg)
{
    (void)arg;
    int16_t chunk[CHUNK_SAMPLES];

    for (;;) {
        uint32_t cmd = 0;
        TickType_t wait = s_playing ? 0 : portMAX_DELAY;
        if (xTaskNotifyWait(0, UINT32_MAX, &cmd, wait) == pdTRUE) {
            if (cmd == AUDIO_CMD_STOP) {
                break;
            }
            if (cmd == AUDIO_CMD_APPLY) {
                if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
                    apply_locked_pattern();
                    xSemaphoreGive(s_lock);
                }
            }
        }

        if (s_cancel) {
            continue;
        }
        if (!s_playing) {
            continue;
        }
        if (!s_audio_ok) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            beatbox_voice_render(&s_voice, chunk, CHUNK_SAMPLES);
            xSemaphoreGive(s_lock);
        } else {
            memset(chunk, 0, sizeof(chunk));
        }

        if (bsp_audio_write(chunk, sizeof(chunk)) != ESP_OK) {
            ESP_LOGW(TAG, "playback write failed; pausing loop");
            s_playing = false;
        }
    }

    xSemaphoreGive(s_stopped);
    for (;;) {
        vTaskSuspend(NULL);
    }
}

esp_err_t beatbox_audio_start(void)
{
    if (s_task) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    s_stopped = xSemaphoreCreateBinary();
    if (!s_lock || !s_stopped) {
        if (s_lock) {
            vSemaphoreDelete(s_lock);
            s_lock = NULL;
        }
        if (s_stopped) {
            vSemaphoreDelete(s_stopped);
            s_stopped = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    s_cancel = false;
    s_playing = false;
    beatbox_pattern_for_style(BEATBOX_STYLE_TRAP, &s_pattern);
    s_pattern_ready = true;
    beatbox_voice_init(&s_voice, &s_pattern);

    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init failed: %s", esp_err_to_name(err));
        s_audio_ok = false;
    } else {
        err = bsp_audio_set_format(BEATBOX_SAMPLE_RATE, 16, 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "set_format failed: %s", esp_err_to_name(err));
            s_audio_ok = false;
        } else {
            bsp_audio_set_volume(78);
            s_audio_ok = true;
        }
    }

    if (xTaskCreate(audio_task, "bb_audio", AUDIO_TASK_STACK, NULL, 5, &s_task) != pdPASS) {
        s_task = NULL;
        vSemaphoreDelete(s_lock);
        vSemaphoreDelete(s_stopped);
        s_lock = NULL;
        s_stopped = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t beatbox_audio_stop(void)
{
    if (!s_task) {
        return ESP_OK;
    }
    s_playing = false;
    s_cancel = true;
    xTaskNotify(s_task, AUDIO_CMD_STOP, eSetValueWithOverwrite);
    if (xSemaphoreTake(s_stopped, pdMS_TO_TICKS(AUDIO_STOP_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "audio task stop timed out");
    }
    vTaskDelete(s_task);
    s_task = NULL;
    vSemaphoreDelete(s_lock);
    vSemaphoreDelete(s_stopped);
    s_lock = NULL;
    s_stopped = NULL;
    (void)bsp_audio_sleep();
    return ESP_OK;
}

void beatbox_audio_set_pattern(const beatbox_pattern_t *pattern)
{
    if (!pattern || !s_lock) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_pattern = *pattern;
        s_pattern_ready = true;
        apply_locked_pattern();
        xSemaphoreGive(s_lock);
    }
    if (s_task) {
        xTaskNotify(s_task, AUDIO_CMD_APPLY, eSetValueWithOverwrite);
    }
}

void beatbox_audio_set_playing(bool playing)
{
    if (!s_audio_ok) {
        s_playing = false;
        return;
    }
    s_playing = playing;
    if (playing && s_task) {
        xTaskNotify(s_task, AUDIO_CMD_APPLY, eSetValueWithOverwrite);
    }
}

bool beatbox_audio_is_playing(void)
{
    return s_playing;
}
