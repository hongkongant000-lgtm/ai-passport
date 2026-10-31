#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    APP_GOOGLE_MODE_MOCK = 0,
    APP_GOOGLE_MODE_LIVE,
} app_google_mode_t;

app_google_mode_t app_google_mode(void);

// Speech-to-text for 16 kHz mono PCM16. Uses mock text when no API key / no Wi-Fi /
// insufficient heap. Never logs the API key.
esp_err_t app_google_stt(const int16_t *pcm, size_t samples,
                         char *text, size_t text_len, bool *used_mock);

// Cloud Translation. Mock: prefix "[tr] " or a fixed EN/ZH map for demo phrases.
esp_err_t app_google_translate(const char *source,
                               char *translated, size_t translated_len,
                               bool *used_mock);
