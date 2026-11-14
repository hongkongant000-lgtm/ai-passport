#include "meihua_ui.h"

#include "bsp_battery.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "meihua_ui";

/* Palette: ink-on-paper with a single vermilion accent (not the demo shell). */
#define MH_INK        0x1A1A1A
#define MH_PAPER      0xF3EDE2
#define MH_PANEL      0xE8DFD0
#define MH_ACCENT     0xB53A2D
#define MH_MUTED      0x6B6358
#define MH_LINE_YANG  0x1A1A1A
#define MH_LINE_YIN   0x1A1A1A

LV_FONT_DECLARE(meihua_font_16);

static const char *const s_questions[] = {
    "今日宜否进取？",
    "所问之事吉凶？",
    "合作能否顺利？",
    "出行是否平安？",
    "求学进度如何？",
};

static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_battery;
static lv_obj_t *s_body;
static lv_obj_t *s_hint;
static lv_obj_t *s_hex_box;
static meihua_app_t *s_app;

int meihua_question_count(void)
{
    return (int)(sizeof(s_questions) / sizeof(s_questions[0]));
}

const char *meihua_question_text(int index)
{
    int n = meihua_question_count();
    if (n <= 0) {
        return "";
    }
    if (index < 0) {
        index = 0;
    }
    index %= n;
    return s_questions[index];
}

static void style_label(lv_obj_t *label, lv_color_t color)
{
    lv_obj_set_style_text_font(label, &meihua_font_16, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static void clear_body(void)
{
    if (s_body) {
        lv_obj_clean(s_body);
    }
    s_hex_box = NULL;
}

static void draw_hexagram(lv_obj_t *parent, int x, int y, const uint8_t lines[6],
                          uint8_t moving_line, const char *caption)
{
    lv_obj_t *cap = lv_label_create(parent);
    style_label(cap, lv_color_hex(MH_MUTED));
    lv_label_set_text(cap, caption);
    lv_obj_set_pos(cap, x, y);

    for (int i = 0; i < 6; i++) {
        /* Draw top line first visually: line 6 at top. */
        int visual = 5 - i;
        int yy = y + 18 + visual * 12;
        bool yang = lines[i] != 0;
        bool moving = (moving_line == (uint8_t)(i + 1));

        if (yang) {
            lv_obj_t *bar = lv_obj_create(parent);
            lv_obj_set_size(bar, 54, 5);
            lv_obj_set_pos(bar, x, yy);
            lv_obj_set_style_radius(bar, 0, 0);
            lv_obj_set_style_border_width(bar, 0, 0);
            lv_obj_set_style_bg_color(bar, lv_color_hex(MH_LINE_YANG), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        } else {
            lv_obj_t *left = lv_obj_create(parent);
            lv_obj_set_size(left, 22, 5);
            lv_obj_set_pos(left, x, yy);
            lv_obj_set_style_radius(left, 0, 0);
            lv_obj_set_style_border_width(left, 0, 0);
            lv_obj_set_style_bg_color(left, lv_color_hex(MH_LINE_YIN), 0);
            lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
            lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *right = lv_obj_create(parent);
            lv_obj_set_size(right, 22, 5);
            lv_obj_set_pos(right, x + 32, yy);
            lv_obj_set_style_radius(right, 0, 0);
            lv_obj_set_style_border_width(right, 0, 0);
            lv_obj_set_style_bg_color(right, lv_color_hex(MH_LINE_YIN), 0);
            lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
            lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
        }

        if (moving) {
            lv_obj_t *mark = lv_label_create(parent);
            style_label(mark, lv_color_hex(MH_ACCENT));
            lv_label_set_text(mark, "动");
            lv_obj_set_pos(mark, x + 56, yy - 6);
        }
    }
}

static bool cast_now(meihua_app_t *app)
{
    time_t now = time(NULL);
    struct tm tm_now;
    if (now == (time_t)-1 || !localtime_r(&now, &tm_now)) {
        /* Device clock unset: use a fixed documented fallback for offline casting. */
        ESP_LOGW(TAG, "system time unavailable; using fallback civil time");
        app->used_time.year = 2026;
        app->used_time.month = 11;
        app->used_time.day = 15;
        app->used_time.hour = 10;
    } else {
        app->used_time.year = tm_now.tm_year + 1900;
        app->used_time.month = tm_now.tm_mon + 1;
        app->used_time.day = tm_now.tm_mday;
        app->used_time.hour = tm_now.tm_hour;
        /* If RTC is still at epoch, treat as unset and use the same fallback. */
        if (app->used_time.year < 2020) {
            ESP_LOGW(TAG, "RTC looks unset (%d); using fallback civil time",
                     app->used_time.year);
            app->used_time.year = 2026;
            app->used_time.month = 11;
            app->used_time.day = 15;
            app->used_time.hour = 10;
        }
    }
    app->cast_valid = meihua_cast_from_civil(&app->used_time, &app->cast);
    return app->cast_valid;
}

void meihua_ui_refresh_battery(meihua_app_t *app)
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

void meihua_ui_show_ask(meihua_app_t *app)
{
    if (!app || !s_screen) {
        return;
    }
    app->page = MEIHUA_PAGE_ASK;
    clear_body();

    lv_label_set_text(s_title, "梅花易数");
    lv_label_set_text(s_hint, "上/下选题  确定起卦");

    lv_obj_t *prompt = lv_label_create(s_body);
    style_label(prompt, lv_color_hex(MH_MUTED));
    lv_label_set_text(prompt, "所问");
    lv_obj_set_pos(prompt, 0, 8);

    lv_obj_t *card = lv_obj_create(s_body);
    lv_obj_set_size(card, 208, 88);
    lv_obj_set_pos(card, 0, 32);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(MH_PANEL), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *q = lv_label_create(card);
    style_label(q, lv_color_hex(MH_INK));
    lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(q, 180);
    lv_label_set_text(q, meihua_question_text(app->question_index));
    lv_obj_center(q);

    lv_obj_t *sub = lv_label_create(s_body);
    style_label(sub, lv_color_hex(MH_MUTED));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(sub, 208);
    lv_label_set_text(sub, "默认时间起卦\n年月日时即刻起数");
    lv_obj_set_pos(sub, 0, 136);

    meihua_ui_refresh_battery(app);
}

void meihua_ui_show_result(meihua_app_t *app)
{
    if (!app || !s_screen || !app->cast_valid) {
        return;
    }
    app->page = MEIHUA_PAGE_RESULT;
    clear_body();

    lv_label_set_text(s_title, "卦象");
    lv_label_set_text(s_hint, "确定返回  长按再问");

    const meihua_cast_t *c = &app->cast;
    char line[96];

    lv_obj_t *q = lv_label_create(s_body);
    style_label(q, lv_color_hex(MH_MUTED));
    lv_label_set_long_mode(q, LV_LABEL_LONG_DOT);
    lv_obj_set_width(q, 208);
    lv_label_set_text(q, meihua_question_text(app->question_index));
    lv_obj_set_pos(q, 0, 0);

    snprintf(line, sizeof(line), "%04d-%02d-%02d %02d时  动%d",
             app->used_time.year, app->used_time.month, app->used_time.day,
             app->used_time.hour, c->moving_line);
    lv_obj_t *when = lv_label_create(s_body);
    style_label(when, lv_color_hex(MH_MUTED));
    lv_label_set_text(when, line);
    lv_obj_set_pos(when, 0, 20);

    s_hex_box = lv_obj_create(s_body);
    lv_obj_set_size(s_hex_box, 208, 110);
    lv_obj_set_pos(s_hex_box, 0, 42);
    lv_obj_set_style_radius(s_hex_box, 8, 0);
    lv_obj_set_style_border_width(s_hex_box, 0, 0);
    lv_obj_set_style_bg_color(s_hex_box, lv_color_hex(MH_PANEL), 0);
    lv_obj_set_style_bg_opa(s_hex_box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_hex_box, LV_OBJ_FLAG_SCROLLABLE);

    draw_hexagram(s_hex_box, 8, 4, c->ben_lines, c->moving_line, "本");
    draw_hexagram(s_hex_box, 78, 4, c->hu_lines, 0, "互");
    draw_hexagram(s_hex_box, 148, 4, c->bian_lines, 0, "变");

    snprintf(line, sizeof(line), "本 %s%s  互 %s%s  变 %s%s",
             meihua_trigram_name(c->upper), meihua_trigram_name(c->lower),
             meihua_trigram_name(c->hu_upper), meihua_trigram_name(c->hu_lower),
             meihua_trigram_name(c->bian_upper), meihua_trigram_name(c->bian_lower));
    lv_obj_t *names = lv_label_create(s_body);
    style_label(names, lv_color_hex(MH_INK));
    lv_label_set_long_mode(names, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(names, 208);
    lv_label_set_text(names, line);
    lv_obj_set_pos(names, 0, 158);

    snprintf(line, sizeof(line), "体%s 用%s",
             meihua_trigram_name(c->ti), meihua_trigram_name(c->yong));
    lv_obj_t *tiyong = lv_label_create(s_body);
    style_label(tiyong, lv_color_hex(MH_ACCENT));
    lv_label_set_text(tiyong, line);
    lv_obj_set_pos(tiyong, 0, 186);

    lv_obj_t *verdict = lv_label_create(s_body);
    style_label(verdict, lv_color_hex(MH_INK));
    lv_label_set_long_mode(verdict, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(verdict, 208);
    lv_label_set_text(verdict, meihua_relation_verdict(c->relation));
    lv_obj_set_pos(verdict, 0, 208);

    meihua_ui_refresh_battery(app);
}

void meihua_ui_init(meihua_app_t *app)
{
    s_app = app;
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(MH_PAPER), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_title = lv_label_create(s_screen);
    style_label(s_title, lv_color_hex(MH_INK));
    lv_obj_set_pos(s_title, 16, 10);

    s_battery = lv_label_create(s_screen);
    style_label(s_battery, lv_color_hex(MH_MUTED));
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -16, 10);

    s_body = lv_obj_create(s_screen);
    lv_obj_set_size(s_body, 208, 250);
    lv_obj_set_pos(s_body, 16, 36);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_body, 0, 0);
    lv_obj_set_style_pad_all(s_body, 0, 0);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);

    s_hint = lv_label_create(s_screen);
    style_label(s_hint, lv_color_hex(MH_MUTED));
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_screen_load(s_screen);
    meihua_ui_show_ask(app);
}

void meihua_ui_on_key(meihua_app_t *app, bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!app) {
        return;
    }

    if (app->page == MEIHUA_PAGE_ASK) {
        if (ev != BSP_BTN_CLICK) {
            return;
        }
        if (btn == BSP_BTN_UP) {
            int n = meihua_question_count();
            app->question_index = (app->question_index + n - 1) % n;
            meihua_ui_show_ask(app);
        } else if (btn == BSP_BTN_DOWN) {
            int n = meihua_question_count();
            app->question_index = (app->question_index + 1) % n;
            meihua_ui_show_ask(app);
        } else if (btn == BSP_BTN_OK) {
            if (cast_now(app)) {
                meihua_ui_show_result(app);
            }
        }
        return;
    }

    if (app->page == MEIHUA_PAGE_RESULT) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            meihua_ui_show_ask(app);
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            if (cast_now(app)) {
                meihua_ui_show_result(app);
            }
        }
    }
}
