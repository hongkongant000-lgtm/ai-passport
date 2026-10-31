#include "app_listen.h"
#include "app_audio_mode.h"
#include "app_config.h"
#include "app_xai.h"
#include "app_store.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_listen";

static TaskHandle_t s_task;
static volatile bool s_cancel;
static volatile bool s_busy;
static app_listen_done_cb_t s_cb;
static void *s_user;

static void listen_task(void *arg)
{
    (void)arg;
    char source[APP_TEXT_MAX];
    char translation[APP_TEXT_MAX];
    source[0] = translation[0] = '\0';
    bool mocked = false;
    esp_err_t err = ESP_OK;

    if (!app_audio_mode_try_listen()) {
        err = ESP_ERR_INVALID_STATE;
        goto finish;
    }

    size_t samples = APP_LISTEN_MAX_SAMPLES;
    int16_t *pcm = calloc(samples, sizeof(int16_t));
    if (!pcm) {
        err = ESP_ERR_NO_MEM;
        app_audio_mode_end_listen();
        goto finish;
    }

    err = bsp_audio_set_format(APP_AUDIO_HZ, 16, 1);
    if (err != ESP_OK) {
        free(pcm);
        app_audio_mode_end_listen();
        goto finish;
    }

    ESP_LOGI(TAG, "recording %d ms", APP_LISTEN_MS);
    size_t got = 0;
    const size_t chunk = 512;
    while (got < samples && !s_cancel) {
        size_t n = samples - got;
        if (n > chunk) n = chunk;
        if (bsp_audio_read(pcm + got, n * sizeof(int16_t)) != ESP_OK) {
            err = ESP_FAIL;
            break;
        }
        got += n;
    }

    if (s_cancel) {
        err = ESP_ERR_INVALID_STATE;
        free(pcm);
        app_audio_mode_end_listen();
        goto finish;
    }

    bool stt_mock = false;
    err = app_xai_stt(pcm, got, source, sizeof(source), &stt_mock);
    free(pcm);
    pcm = NULL;
    if (err != ESP_OK) {
        app_audio_mode_end_listen();
        goto finish;
    }

    bool tr_mock = false;
    err = app_xai_translate(source, translation, sizeof(translation), &tr_mock);
    mocked = stt_mock || tr_mock;
    if (err == ESP_OK) {
        (void)app_store_save(source, translation, NULL, 0);
    }
    app_audio_mode_end_listen();

finish:
    if (s_cb) s_cb(source, translation, mocked, err, s_user);
    s_busy = false;
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t app_listen_start(app_listen_done_cb_t cb, void *user)
{
    if (s_busy) return ESP_ERR_INVALID_STATE;
    s_cb = cb;
    s_user = user;
    s_cancel = false;
    s_busy = true;
    if (xTaskCreate(listen_task, "app_listen", 8192, NULL, 5, &s_task) != pdPASS) {
        s_busy = false;
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_listen_cancel(void)
{
    s_cancel = true;
}

bool app_listen_busy(void)
{
    return s_busy;
}
