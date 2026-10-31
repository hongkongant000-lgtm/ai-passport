// Pure navigation state for the translator UI (host-testable, no ESP-IDF/LVGL).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    APP_PAGE_HOME = 0,
    APP_PAGE_SPEAKER,
    APP_PAGE_ARCHIVE,
    APP_PAGE_SETTINGS,
    APP_PAGE_COUNT
} app_page_t;

typedef enum {
    APP_NAV_INPUT_UP = 0,
    APP_NAV_INPUT_DOWN,
    APP_NAV_INPUT_OK,
    APP_NAV_INPUT_BACK,   // OK long-press
    APP_NAV_INPUT_OTHER
} app_nav_input_t;

typedef enum {
    APP_NAV_ACTION_NONE = 0,
    APP_NAV_ACTION_REFRESH,     // focus/page chrome changed
    APP_NAV_ACTION_ENTER_PAGE,  // switched page
    APP_NAV_ACTION_ACTIVATE,    // OK on focused item
} app_nav_action_t;

typedef struct {
    app_page_t page;
    int focus;          // page-local focus index
    int home_count;     // focus slots on home
    int speaker_count;
    int archive_count;
    int settings_count;
} app_nav_t;

typedef struct {
    app_nav_action_t action;
    app_page_t page;
    int focus;
} app_nav_result_t;

void app_nav_init(app_nav_t *nav);
app_nav_result_t app_nav_handle(app_nav_t *nav, app_nav_input_t input);
int app_nav_focus_count(const app_nav_t *nav);
void app_nav_set_archive_count(app_nav_t *nav, int count);
void app_nav_set_speaker_count(app_nav_t *nav, int count);
