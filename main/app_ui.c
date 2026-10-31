// Redesigned translator shell — not the baseline demo menu / ui_pixel theme.
#include "app_ui.h"
#include "app_ble.h"
#include "app_blufi.h"
#include "app_cloud.h"
#include "app_config.h"
#include "app_nav.h"
#include "app_power.h"
#include "app_wifi.h"

#include "bsp_battery.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "app_ui";

// Visual language: deep ink field, chalk text, amber focus — distinct from ui_pixel.
#define COL_BG       0x0B1C24
#define COL_PANEL    0x122B36
#define COL_INK      0xE7F0F4
#define COL_MUTED    0x7FA0AE
#define COL_ACCENT   0xE2A04A
#define COL_OK       0x4CAF7A

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static app_nav_t s_nav;
static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static lv_obj_t *s_scr;
static lv_obj_t *s_brand;
static lv_obj_t *s_battery;
static lv_obj_t *s_ble_badge;
static lv_obj_t *s_wifi_badge;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_hint;
static lv_timer_t *s_timer;
static bool s_listening;
static char s_source_line[96];
static char s_trans_line[96];
static int s_volume = 60;

static void style_label(lv_obj_t *obj, const lv_font_t *font, uint32_t color)
{
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}

static const char *ble_label(void)
{
    if (app_blufi_is_active()) {
        switch (app_blufi_state()) {
        case APP_BLUFI_ADVERTISING: return "BLUFI";
        case APP_BLUFI_BLE_CONNECTED: return "BLUFI+";
        case APP_BLUFI_WIFI_CONNECTING: return "BLUFI~";
        case APP_BLUFI_WIFI_CONNECTED: return "BLUFI OK";
        case APP_BLUFI_FAILED: return "BLUFI!";
        default: return "BLUFI...";
        }
    }
    switch (app_ble_state()) {
    case APP_BLE_ADVERTISING: return "BLE ADV";
    case APP_BLE_CONNECTED: return "BLE ON";
    case APP_BLE_FAILED: return "BLE!";
    case APP_BLE_STARTING: return "BLE...";
    default: return "BLE OFF";
    }
}

static const char *wifi_label(void)
{
    switch (app_wifi_state()) {
    case APP_WIFI_CONNECTED: return "WiFi ON";
    case APP_WIFI_CONNECTING: return "WiFi...";
    case APP_WIFI_DISCONNECTED: return "WiFi --";
    case APP_WIFI_FAILED: return "WiFi!";
    case APP_WIFI_STARTING: return "WiFi...";
    default: return "WiFi OFF";
    }
}

static void refresh_chrome(void)
{
    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_label_set_text(s_battery, "");
    } else {
        lv_label_set_text_fmt(s_battery, "%d%%", soc);
    }
    lv_label_set_text(s_ble_badge, ble_label());
    lv_label_set_text(s_wifi_badge, wifi_label());
    uint32_t ble_color = (app_ble_is_connected() || app_blufi_state() == APP_BLUFI_BLE_CONNECTED ||
                          app_blufi_state() == APP_BLUFI_WIFI_CONNECTED)
                             ? COL_OK : COL_MUTED;
    uint32_t wifi_color = app_wifi_got_ip() ? COL_OK : COL_MUTED;
    lv_obj_set_style_text_color(s_ble_badge, lv_color_hex(ble_color), 0);
    lv_obj_set_style_text_color(s_wifi_badge, lv_color_hex(wifi_color), 0);
}

static void render_home(void)
{
    lv_label_set_text(s_title, "Listen & Translate");
    char body[256];
    snprintf(body, sizeof(body),
             "%s\n%s\n%s\n%s\n\nSRC: %s\nTR:  %s",
             s_nav.focus == 0 ? (s_listening ? "* Stop listen" : "* Start listen")
                              : (s_listening ? "  Stop listen" : "  Start listen"),
             s_nav.focus == 1 ? "* Speaker" : "  Speaker",
             s_nav.focus == 2 ? "* Archive" : "  Archive",
             s_nav.focus == 3 ? "* Settings" : "  Settings",
             s_source_line[0] ? s_source_line : "(none yet)",
             s_trans_line[0] ? s_trans_line : "(none yet)");
    lv_label_set_text(s_body, body);
    lv_label_set_text(s_hint, "UP/DOWN  OK open  hold OK back");
}

static void render_speaker(void)
{
    lv_label_set_text(s_title, "Speaker");
    char body[160];
    snprintf(body, sizeof(body),
             "Wi-Fi audio stream\n(from phone)\n\n%s Volume -\n%s Volume +  (%d%%)\n\n%s",
             s_nav.focus == 0 ? "*" : " ",
             s_nav.focus == 1 ? "*" : " ",
             s_volume,
             "Playback stub — milestone 1");
    lv_label_set_text(s_body, body);
    lv_label_set_text(s_hint, "hold OK = home");
}

static void render_archive(void)
{
    lv_label_set_text(s_title, "Archive");
    lv_label_set_text(s_body,
                      "* (empty)\n\nTranscript save\ncomes in a later\nmilestone.\n\nSPIFFS partition\nready: /store");
    lv_label_set_text(s_hint, "hold OK = home");
}

static void render_settings(void)
{
    lv_label_set_text(s_title, "Settings");
    char ssid[33] = { 0 };
    char ip[16] = { 0 };
    app_wifi_get_ssid(ssid, sizeof(ssid));
    app_wifi_get_ip(ip, sizeof(ip));
    char body[320];
    snprintf(body, sizeof(body),
             "%s %s\n%s Clear Wi-Fi\n%s Lang: %s\n%s Cloud: %s\n\nSSID: %s\nIP: %s\nBLE: %s",
             s_nav.focus == 0 ? "*" : " ",
             app_blufi_is_active() ? "Stop BLUFI" : "Start BLUFI",
             s_nav.focus == 1 ? "*" : " ",
             s_nav.focus == 2 ? "*" : " ",
             app_cloud_lang_label(),
             s_nav.focus == 3 ? "*" : " ",
             app_cloud_api_key_configured() ? "key set" : "key unset",
             ssid[0] ? ssid : "(none)",
             ip[0] ? ip : "--",
             APP_DEVICE_NAME);
    lv_label_set_text(s_body, body);
    lv_label_set_text(s_hint, "OK act  hold OK home");
}

static void render_page(void)
{
    refresh_chrome();
    switch (s_nav.page) {
    case APP_PAGE_HOME: render_home(); break;
    case APP_PAGE_SPEAKER: render_speaker(); break;
    case APP_PAGE_ARCHIVE: render_archive(); break;
    case APP_PAGE_SETTINGS: render_settings(); break;
    default: break;
    }
}

static void on_activate(void)
{
    if (s_nav.page == APP_PAGE_HOME && s_nav.focus == 0) {
        s_listening = !s_listening;
        if (s_listening) {
            strncpy(s_source_line, "[listening...]", sizeof(s_source_line) - 1);
            strncpy(s_trans_line, "(STT later)", sizeof(s_trans_line) - 1);
            app_ble_set_status_text("listen");
        } else {
            strncpy(s_source_line, "(stopped)", sizeof(s_source_line) - 1);
            app_ble_set_status_text("idle");
        }
        return;
    }
    if (s_nav.page == APP_PAGE_SPEAKER) {
        if (s_nav.focus == 0 && s_volume >= 10) s_volume -= 10;
        if (s_nav.focus == 1 && s_volume <= 90) s_volume += 10;
        return;
    }
    if (s_nav.page == APP_PAGE_SETTINGS) {
        if (s_nav.focus == 0) {
            if (app_blufi_is_active()) {
                app_blufi_stop();
                (void)app_ble_start();
            } else {
                esp_err_t err = app_blufi_start();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "BLUFI start: %s", esp_err_to_name(err));
                }
            }
        } else if (s_nav.focus == 1) {
            (void)app_wifi_clear_credentials();
        } else if (s_nav.focus == 2) {
            app_cloud_cycle_lang_pair();
        } else if (s_nav.focus == 3) {
            // Placeholder: show status only. Keys are written via NVS / phone later.
            ESP_LOGI(TAG, "Google API key is %s (set via NVS, never commit)",
                     app_cloud_api_key_configured() ? "configured" : "missing");
        }
    }
}

static void process_input(const input_event_t *input)
{
    app_nav_input_t nav_input = APP_NAV_INPUT_OTHER;
    if (input->event == BSP_BTN_CLICK) {
        if (input->btn == BSP_BTN_UP) nav_input = APP_NAV_INPUT_UP;
        else if (input->btn == BSP_BTN_DOWN) nav_input = APP_NAV_INPUT_DOWN;
        else if (input->btn == BSP_BTN_OK) nav_input = APP_NAV_INPUT_OK;
    } else if (input->event == BSP_BTN_LONG && input->btn == BSP_BTN_OK) {
        nav_input = APP_NAV_INPUT_BACK;
    }
    if (nav_input == APP_NAV_INPUT_OTHER) return;

    app_nav_result_t result = app_nav_handle(&s_nav, nav_input);
    if (result.action == APP_NAV_ACTION_ACTIVATE) on_activate();
    if (result.action == APP_NAV_ACTION_NONE) return;

    if (!bsp_lvgl_lock(200)) return;
    render_page();
    bsp_lvgl_unlock();
}

static void input_task(void *arg)
{
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            process_input(&input);
        }
    }
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    refresh_chrome();
    // Keep settings page live while provisioning.
    if (s_nav.page == APP_PAGE_SETTINGS || s_nav.page == APP_PAGE_HOME) {
        render_page();
    }
    if (app_wifi_got_ip() && app_blufi_is_active() &&
        app_blufi_state() == APP_BLUFI_WIFI_CONNECTING) {
        // State will flip via wifi callbacks; UI refresh is enough.
    }
}

static void build_shell(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = lv_obj_create(s_scr);
    lv_obj_set_size(top, 240, 36);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(top, 0, 0);

    s_brand = lv_label_create(top);
    style_label(s_brand, &lv_font_montserrat_14, COL_ACCENT);
    lv_label_set_text(s_brand, "PASSPORT");
    lv_obj_set_pos(s_brand, 8, 10);

    s_battery = lv_label_create(top);
    style_label(s_battery, &lv_font_montserrat_14, COL_INK);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -8, 10);

    s_ble_badge = lv_label_create(s_scr);
    style_label(s_ble_badge, &lv_font_montserrat_14, COL_MUTED);
    lv_obj_set_pos(s_ble_badge, 8, 44);

    s_wifi_badge = lv_label_create(s_scr);
    style_label(s_wifi_badge, &lv_font_montserrat_14, COL_MUTED);
    lv_obj_set_pos(s_wifi_badge, 100, 44);

    s_title = lv_label_create(s_scr);
    style_label(s_title, &lv_font_montserrat_20, COL_INK);
    lv_obj_set_pos(s_title, 8, 68);
    lv_obj_set_width(s_title, 224);

    lv_obj_t *panel = lv_obj_create(s_scr);
    lv_obj_set_size(panel, 224, 178);
    lv_obj_set_pos(panel, 8, 100);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    s_body = lv_label_create(panel);
    style_label(s_body, &lv_font_montserrat_14, COL_INK);
    lv_obj_set_width(s_body, 200);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);

    s_hint = lv_label_create(s_scr);
    style_label(s_hint, &lv_font_montserrat_14, COL_MUTED);
    lv_obj_set_pos(s_hint, 8, 288);
    lv_obj_set_width(s_hint, 224);

    strncpy(s_source_line, "", sizeof(s_source_line));
    strncpy(s_trans_line, "", sizeof(s_trans_line));
    app_nav_init(&s_nav);
    render_page();
    lv_screen_load(s_scr);
    s_timer = lv_timer_create(tick, 500, NULL);
}

esp_err_t app_ui_start(void)
{
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(input_task, "app_input", 4096, NULL, 5, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
    build_shell();
    bsp_lvgl_unlock();
    return ESP_OK;
}

void app_ui_on_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_input_queue) return;
    input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}
