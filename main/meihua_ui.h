// main/meihua_ui.h — redesigned LVGL shell for Mei Hua Yi Shu (not ui_pixel).
#pragma once

#include "bsp_button.h"
#include "meihua_cast.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEIHUA_PAGE_ASK = 0,
    MEIHUA_PAGE_RESULT,
} meihua_page_t;

typedef struct {
    meihua_page_t page;
    int question_index;
    meihua_cast_t cast;
    meihua_civil_time_t used_time;
    bool cast_valid;
    bool battery_ok;
} meihua_app_t;

void meihua_ui_init(meihua_app_t *app);
void meihua_ui_show_ask(meihua_app_t *app);
void meihua_ui_show_result(meihua_app_t *app);
void meihua_ui_refresh_battery(meihua_app_t *app);
void meihua_ui_on_key(meihua_app_t *app, bsp_btn_t btn, bsp_btn_ev_t ev);

/* Question bank count for host tests / UI. */
int meihua_question_count(void);
const char *meihua_question_text(int index);

#ifdef __cplusplus
}
#endif
