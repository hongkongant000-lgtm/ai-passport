#include <assert.h>
#include "app_nav.h"

int main(void)
{
    app_nav_t nav;
    app_nav_init(&nav);
    assert(nav.page == APP_PAGE_HOME);
    assert(nav.focus == 0);

    app_nav_result_t r = app_nav_handle(&nav, APP_NAV_INPUT_DOWN);
    assert(r.action == APP_NAV_ACTION_REFRESH);
    assert(nav.focus == 1);

    r = app_nav_handle(&nav, APP_NAV_INPUT_OK);
    assert(r.action == APP_NAV_ACTION_ENTER_PAGE);
    assert(nav.page == APP_PAGE_SPEAKER);
    assert(nav.focus == 0);

    r = app_nav_handle(&nav, APP_NAV_INPUT_OK);
    assert(r.action == APP_NAV_ACTION_ACTIVATE);

    r = app_nav_handle(&nav, APP_NAV_INPUT_BACK);
    assert(r.action == APP_NAV_ACTION_ENTER_PAGE);
    assert(nav.page == APP_PAGE_HOME);

    r = app_nav_handle(&nav, APP_NAV_INPUT_OTHER);
    assert(r.action == APP_NAV_ACTION_NONE);

    // Home listen activate
    nav.focus = 0;
    r = app_nav_handle(&nav, APP_NAV_INPUT_OK);
    assert(r.action == APP_NAV_ACTION_ACTIVATE);
    assert(nav.page == APP_PAGE_HOME);

    // Wrap focus
    nav.focus = 3;
    r = app_nav_handle(&nav, APP_NAV_INPUT_DOWN);
    assert(nav.focus == 0);
    r = app_nav_handle(&nav, APP_NAV_INPUT_UP);
    assert(nav.focus == 3);

    app_nav_set_archive_count(&nav, 5);
    assert(nav.archive_count == 5);
    app_nav_set_speaker_count(&nav, 3);
    assert(nav.speaker_count == 3);

    return 0;
}
