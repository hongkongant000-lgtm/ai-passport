// main/beatbox_ui.h — redesigned LVGL studio shell for Beatbox Passport (not ui_pixel).
#pragma once

#include "beatbox_engine.h"
#include "bsp_button.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    beatbox_pattern_t pattern;
    bool playing;
    bool battery_ok;
    bool audio_ok;
    bool bpm_from_stub;
    char display_name[24];
} beatbox_app_t;

void beatbox_ui_init(beatbox_app_t *app);
void beatbox_ui_refresh(beatbox_app_t *app);
void beatbox_ui_refresh_battery(beatbox_app_t *app);
void beatbox_ui_on_key(beatbox_app_t *app, bsp_btn_t btn, bsp_btn_ev_t ev);

#ifdef __cplusplus
}
#endif
