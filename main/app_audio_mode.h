#pragma once

#include <stdbool.h>

// Mutual exclusion between mic listen and Wi-Fi speaker playback.
bool app_audio_mode_try_listen(void);
void app_audio_mode_end_listen(void);
bool app_audio_mode_try_speaker(void);
void app_audio_mode_end_speaker(void);
bool app_audio_mode_listening(void);
bool app_audio_mode_speaking(void);
