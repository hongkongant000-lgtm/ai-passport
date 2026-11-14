// main/beatbox_ui.c — Passport-style beatbox studio UI (mandatory redesign).
#include "beatbox_ui.h"

#include "beatbox_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "beatbox_ui";

/* Studio palette: deep charcoal stage + lime accent (not demo / meihua shell). */
#define BB_BG        0x12151C
#define BB_PANEL     0x1C2230
#define BB_INK       0xE8EDF5
#define BB_MUTED     0x8A93A6
#define BB_ACCENT    0xB8F04A
#define BB_ACCENT_DIM 0x5A7A2A
#define BB_AVATAR    0x2A3348
#define BB_STEP_ON   0xB8F04A
#define BB_STEP_OFF  0x2E3648
#define BB_PLAY      0x4ADE80
#define BB_STOP      0xF87171

static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_battery;
static lv_obj_t *s_name;
static lv_obj_t *s_style;
static lv_obj_t *s_bpm;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_steps[BEATBOX_STEPS];
static lv_obj_t *s_avatar_ring;

static void style_label(lv_obj_t *label, lv_color_t color, const lv_font_t *font)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static void refresh_steps(const beatbox_app_t *app)
{
    for (int i = 0; i < BEATBOX_STEPS; i++) {
        if (!s_steps[i]) {
            continue;
        }
        bool on = (app->pattern.steps[i] & (BEATBOX_HIT_KICK | BEATBOX_HIT_SNARE |
                                            BEATBOX_HIT_HAT)) != 0;
        bool kick = (app->pattern.steps[i] & BEATBOX_HIT_KICK) != 0;
        lv_obj_set_style_bg_color(
            s_steps[i],
            lv_color_hex(on ? (kick ? BB_ACCENT : BB_STEP_ON) : BB_STEP_OFF),
            0);
        int h = 10;
        if (app->pattern.steps[i] & BEATBOX_HIT_KICK) {
            h = 28;
        } else if (app->pattern.steps[i] & BEATBOX_HIT_SNARE) {
            h = 20;
        } else if (app->pattern.steps[i] & BEATBOX_HIT_HAT) {
            h = 12;
        }
        lv_obj_set_size(s_steps[i], 10, h);
        lv_obj_set_pos(s_steps[i], 12 + i * 13, 36 - h);
    }
}

void beatbox_ui_refresh_battery(beatbox_app_t *app)
{
    if (!s_battery || !app) {
        return;
    }
    if (!app->battery_ok) {
        lv_label_set_text(s_battery, "");
        return;
    }
    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_label_set_text(s_battery, "");
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", soc);
    lv_label_set_text(s_battery, buf);
}

void beatbox_ui_refresh(beatbox_app_t *app)
{
    if (!app || !s_screen) {
        return;
    }

    lv_label_set_text(s_name, app->display_name);
    lv_label_set_text(s_style, beatbox_style_name(app->pattern.style));

    char bpm_buf[24];
    snprintf(bpm_buf, sizeof(bpm_buf), "%d BPM", app->pattern.bpm);
    lv_label_set_text(s_bpm, bpm_buf);

    if (!app->audio_ok) {
        lv_label_set_text(s_status, "Audio unavailable");
        lv_obj_set_style_text_color(s_status, lv_color_hex(BB_STOP), 0);
    } else if (app->playing) {
        lv_label_set_text(s_status, "LOOPING");
        lv_obj_set_style_text_color(s_status, lv_color_hex(BB_PLAY), 0);
    } else {
        lv_label_set_text(s_status, "STOPPED");
        lv_obj_set_style_text_color(s_status, lv_color_hex(BB_MUTED), 0);
    }

    if (app->bpm_from_stub) {
        lv_label_set_text(s_hint, "UP style  DOWN tempo  OK play\nBPM analysis: stub (m2)");
    } else {
        lv_label_set_text(s_hint, "UP style  DOWN tempo  OK play/stop");
    }

    if (s_avatar_ring) {
        lv_obj_set_style_border_color(
            s_avatar_ring,
            lv_color_hex(app->playing ? BB_ACCENT : BB_ACCENT_DIM),
            0);
    }

    refresh_steps(app);
    beatbox_ui_refresh_battery(app);
}

static void apply_style_defaults(beatbox_app_t *app, beatbox_style_t style)
{
    int keep_bpm = app->pattern.bpm;
    bool keep_custom_bpm = app->playing; /* while looping, keep user tempo */
    beatbox_pattern_for_style(style, &app->pattern);
    if (keep_custom_bpm) {
        app->pattern.bpm = beatbox_clamp_bpm(keep_bpm);
    }
    beatbox_audio_set_pattern(&app->pattern);
}

void beatbox_ui_on_key(beatbox_app_t *app, bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!app || ev != BSP_BTN_CLICK) {
        return;
    }

    if (btn == BSP_BTN_UP) {
        apply_style_defaults(app, beatbox_style_next(app->pattern.style));
        ESP_LOGI(TAG, "style -> %s", beatbox_style_name(app->pattern.style));
        beatbox_ui_refresh(app);
        return;
    }

    if (btn == BSP_BTN_DOWN) {
        app->pattern.bpm = beatbox_bump_bpm(app->pattern.bpm);
        beatbox_audio_set_pattern(&app->pattern);
        ESP_LOGI(TAG, "bpm -> %d", app->pattern.bpm);
        beatbox_ui_refresh(app);
        return;
    }

    if (btn == BSP_BTN_OK) {
        if (!app->audio_ok) {
            beatbox_ui_refresh(app);
            return;
        }
        app->playing = !app->playing;
        beatbox_audio_set_playing(app->playing);
        ESP_LOGI(TAG, "playing -> %d", (int)app->playing);
        beatbox_ui_refresh(app);
    }
}

void beatbox_ui_init(beatbox_app_t *app)
{
    if (!app) {
        return;
    }

    if (app->display_name[0] == '\0') {
        strncpy(app->display_name, "Guest", sizeof(app->display_name) - 1);
    }
    if (app->pattern.bpm == 0) {
        beatbox_pattern_for_style(BEATBOX_STYLE_TRAP, &app->pattern);
    }
    app->bpm_from_stub = true; /* milestone 1: analysis stub only */

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(BB_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_title = lv_label_create(s_screen);
    style_label(s_title, lv_color_hex(BB_ACCENT), &lv_font_montserrat_20);
    lv_label_set_text(s_title, "BEATBOX");
    lv_obj_set_pos(s_title, 16, 10);

    s_battery = lv_label_create(s_screen);
    style_label(s_battery, lv_color_hex(BB_MUTED), &lv_font_montserrat_14);
    lv_label_set_text(s_battery, "");
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -12, 12);

    /* Passport identity card */
    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 208, 64);
    lv_obj_set_pos(card, 16, 42);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(BB_PANEL), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_avatar_ring = lv_obj_create(card);
    lv_obj_set_size(s_avatar_ring, 44, 44);
    lv_obj_set_pos(s_avatar_ring, 10, 10);
    lv_obj_set_style_radius(s_avatar_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_avatar_ring, 2, 0);
    lv_obj_set_style_border_color(s_avatar_ring, lv_color_hex(BB_ACCENT_DIM), 0);
    lv_obj_set_style_bg_color(s_avatar_ring, lv_color_hex(BB_AVATAR), 0);
    lv_obj_set_style_bg_opa(s_avatar_ring, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_avatar_ring, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *avatar_mark = lv_label_create(s_avatar_ring);
    style_label(avatar_mark, lv_color_hex(BB_INK), &lv_font_montserrat_14);
    lv_label_set_text(avatar_mark, "BB");
    lv_obj_center(avatar_mark);

    lv_obj_t *name_cap = lv_label_create(card);
    style_label(name_cap, lv_color_hex(BB_MUTED), &lv_font_montserrat_14);
    lv_label_set_text(name_cap, "PASSPORT");
    lv_obj_set_pos(name_cap, 66, 8);

    s_name = lv_label_create(card);
    style_label(s_name, lv_color_hex(BB_INK), &lv_font_montserrat_20);
    lv_label_set_text(s_name, app->display_name);
    lv_obj_set_pos(s_name, 66, 26);

    /* Style + BPM block */
    lv_obj_t *stage = lv_obj_create(s_screen);
    lv_obj_set_size(stage, 208, 118);
    lv_obj_set_pos(stage, 16, 118);
    lv_obj_set_style_radius(stage, 10, 0);
    lv_obj_set_style_border_width(stage, 0, 0);
    lv_obj_set_style_bg_color(stage, lv_color_hex(BB_PANEL), 0);
    lv_obj_set_style_bg_opa(stage, LV_OPA_COVER, 0);
    lv_obj_clear_flag(stage, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *style_cap = lv_label_create(stage);
    style_label(style_cap, lv_color_hex(BB_MUTED), &lv_font_montserrat_14);
    lv_label_set_text(style_cap, "STYLE");
    lv_obj_set_pos(style_cap, 12, 8);

    s_style = lv_label_create(stage);
    style_label(s_style, lv_color_hex(BB_ACCENT), &lv_font_montserrat_20);
    lv_label_set_text(s_style, "");
    lv_obj_set_pos(s_style, 12, 28);

    s_bpm = lv_label_create(stage);
    style_label(s_bpm, lv_color_hex(BB_INK), &lv_font_montserrat_20);
    lv_label_set_text(s_bpm, "");
    lv_obj_set_pos(s_bpm, 12, 54);

    s_status = lv_label_create(stage);
    style_label(s_status, lv_color_hex(BB_MUTED), &lv_font_montserrat_14);
    lv_label_set_text(s_status, "");
    lv_obj_set_pos(s_status, 120, 58);

    /* Step visualizer strip */
    lv_obj_t *strip = lv_obj_create(stage);
    lv_obj_set_size(strip, 184, 40);
    lv_obj_set_pos(strip, 12, 78);
    lv_obj_set_style_radius(strip, 4, 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_bg_color(strip, lv_color_hex(BB_BG), 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < BEATBOX_STEPS; i++) {
        s_steps[i] = lv_obj_create(strip);
        lv_obj_set_style_radius(s_steps[i], 2, 0);
        lv_obj_set_style_border_width(s_steps[i], 0, 0);
        lv_obj_set_style_bg_opa(s_steps[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_steps[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    s_hint = lv_label_create(s_screen);
    style_label(s_hint, lv_color_hex(BB_MUTED), &lv_font_montserrat_14);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint, 208);
    lv_obj_set_pos(s_hint, 16, 252);
    lv_label_set_text(s_hint, "");

    lv_screen_load(s_screen);
    beatbox_ui_refresh(app);
    ESP_LOGI(TAG, "studio UI ready");
}
