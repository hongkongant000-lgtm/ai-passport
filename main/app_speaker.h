#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Wi-Fi TCP PCM sink: phone connects to port APP_SPEAKER_TCP_PORT and streams
// raw 16-bit LE mono @ APP_AUDIO_HZ after an 8-byte "PASSPORT" header.
esp_err_t app_speaker_start(void);
void app_speaker_stop(void);
bool app_speaker_running(void);
bool app_speaker_playing(void);
void app_speaker_set_volume(uint8_t percent);
uint8_t app_speaker_volume(void);
uint16_t app_speaker_port(void);
