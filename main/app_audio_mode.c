#include "app_audio_mode.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_listen;
static bool s_speak;

bool app_audio_mode_try_listen(void)
{
    bool ok = false;
    portENTER_CRITICAL(&s_lock);
    if (!s_listen && !s_speak) {
        s_listen = true;
        ok = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return ok;
}

void app_audio_mode_end_listen(void)
{
    portENTER_CRITICAL(&s_lock);
    s_listen = false;
    portEXIT_CRITICAL(&s_lock);
}

bool app_audio_mode_try_speaker(void)
{
    bool ok = false;
    portENTER_CRITICAL(&s_lock);
    if (!s_listen && !s_speak) {
        s_speak = true;
        ok = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return ok;
}

void app_audio_mode_end_speaker(void)
{
    portENTER_CRITICAL(&s_lock);
    s_speak = false;
    portEXIT_CRITICAL(&s_lock);
}

bool app_audio_mode_listening(void)
{
    bool v;
    portENTER_CRITICAL(&s_lock);
    v = s_listen;
    portEXIT_CRITICAL(&s_lock);
    return v;
}

bool app_audio_mode_speaking(void)
{
    bool v;
    portENTER_CRITICAL(&s_lock);
    v = s_speak;
    portEXIT_CRITICAL(&s_lock);
    return v;
}
