#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    APP_XAI_MODE_MOCK = 0,
    APP_XAI_MODE_LIVE,
} app_xai_mode_t;

app_xai_mode_t app_xai_mode(void);

// Speech-to-text via xAI POST /v1/stt (raw PCM16). Mock when no key / no Wi-Fi / low heap.
esp_err_t app_xai_stt(const int16_t *pcm, size_t samples,
                      char *text, size_t text_len, bool *used_mock);

// Translation via xAI chat completions. Mock when live path unavailable.
esp_err_t app_xai_translate(const char *source,
                            char *translated, size_t translated_len,
                            bool *used_mock);
