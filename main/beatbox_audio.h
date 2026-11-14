// main/beatbox_audio.h — FreeRTOS worker for continuous offline loop playback.
#pragma once

#include "beatbox_engine.h"
#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t beatbox_audio_start(void);
esp_err_t beatbox_audio_stop(void);

void beatbox_audio_set_pattern(const beatbox_pattern_t *pattern);
void beatbox_audio_set_playing(bool playing);
bool beatbox_audio_is_playing(void);

#ifdef __cplusplus
}
#endif
