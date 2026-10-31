#include "app_nav.h"

static int page_count(const app_nav_t *nav)
{
    switch (nav->page) {
    case APP_PAGE_HOME: return nav->home_count;
    case APP_PAGE_SPEAKER: return nav->speaker_count;
    case APP_PAGE_ARCHIVE: return nav->archive_count;
    case APP_PAGE_SETTINGS: return nav->settings_count;
    default: return 0;
    }
}

void app_nav_init(app_nav_t *nav)
{
    nav->page = APP_PAGE_HOME;
    nav->focus = 0;
    // Home: Listen, Speaker, Archive, Settings
    nav->home_count = 4;
    // Speaker: Volume down, Volume up
    nav->speaker_count = 2;
    // Archive: list placeholder (empty → one "empty" slot)
    nav->archive_count = 1;
    // Settings: Provision Wi-Fi, Clear Wi-Fi, Language, Cloud status
    nav->settings_count = 4;
}

int app_nav_focus_count(const app_nav_t *nav)
{
    return page_count(nav);
}

static void clamp_focus(app_nav_t *nav)
{
    int count = page_count(nav);
    if (count <= 0) {
        nav->focus = 0;
        return;
    }
    if (nav->focus < 0) nav->focus = count - 1;
    if (nav->focus >= count) nav->focus = 0;
}

app_nav_result_t app_nav_handle(app_nav_t *nav, app_nav_input_t input)
{
    app_nav_result_t result = {
        .action = APP_NAV_ACTION_NONE,
        .page = nav->page,
        .focus = nav->focus,
    };
    if (input == APP_NAV_INPUT_OTHER) return result;

    if (input == APP_NAV_INPUT_BACK) {
        if (nav->page != APP_PAGE_HOME) {
            nav->page = APP_PAGE_HOME;
            nav->focus = 0;
            result.action = APP_NAV_ACTION_ENTER_PAGE;
            result.page = nav->page;
            result.focus = nav->focus;
        }
        return result;
    }

    if (input == APP_NAV_INPUT_UP) {
        nav->focus--;
        clamp_focus(nav);
        result.action = APP_NAV_ACTION_REFRESH;
        result.focus = nav->focus;
        return result;
    }
    if (input == APP_NAV_INPUT_DOWN) {
        nav->focus++;
        clamp_focus(nav);
        result.action = APP_NAV_ACTION_REFRESH;
        result.focus = nav->focus;
        return result;
    }

    // OK
    if (nav->page == APP_PAGE_HOME) {
        if (nav->focus == 0) {
            result.action = APP_NAV_ACTION_ACTIVATE;
            return result;
        }
        if (nav->focus == 1) nav->page = APP_PAGE_SPEAKER;
        else if (nav->focus == 2) nav->page = APP_PAGE_ARCHIVE;
        else if (nav->focus == 3) nav->page = APP_PAGE_SETTINGS;
        else return result;
        nav->focus = 0;
        result.action = APP_NAV_ACTION_ENTER_PAGE;
        result.page = nav->page;
        result.focus = nav->focus;
        return result;
    }

    result.action = APP_NAV_ACTION_ACTIVATE;
    result.focus = nav->focus;
    return result;
}
