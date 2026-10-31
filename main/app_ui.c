// Redesigned translator shell — not the baseline demo menu / ui_pixel theme.
#include "app_ui.h"
#include "app_ble.h"
#include "app_blufi.h"
#include "app_cloud.h"
#include "app_config.h"
#include "app_xai.h"
#include "app_listen.h"
#include "app_nav.h"
#include "app_speaker.h"
#include "app_store.h"
#include "app_wifi.h"

#include "bsp_battery.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "app_ui";

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
static char s_source_line[APP_TEXT_MAX];
static char s_trans_line[APP_TEXT_MAX];
static char s_status_note[48];
static app_store_entry_t s_archive[APP_STORE_MAX_ENTRIES];
static int s_archive_n;
static bool s_archive_open;
static char s_open_name[24];
static volatile bool s_ui_dirty;

static const lv_font_t *font_ui(void)
{
    // Built-in CJK subset for Chinese chrome + Latin. Device check still required.
    return &lv_font_source_han_sans_sc_16_cjk;
}

static void style_label(lv_obj_t *obj, const lv_font_t *font, uint32_t color)
{
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}

static void refresh_archive_list(void)
{
    s_archive_n = app_store_list(s_archive, APP_STORE_MAX_ENTRIES);
    app_nav_set_archive_count(&s_nav, s_archive_n > 0 ? s_archive_n : 1);
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
    if (soc < 0) lv_label_set_text(s_battery, "");
    else lv_label_set_text_fmt(s_battery, "%d%%", soc);
    lv_label_set_text(s_ble_badge, ble_label());
    lv_label_set_text(s_wifi_badge, wifi_label());
    uint32_t ble_color = (app_ble_is_connected() ||
                          app_blufi_state() == APP_BLUFI_BLE_CONNECTED ||
                          app_blufi_state() == APP_BLUFI_WIFI_CONNECTED)
                             ? COL_OK : COL_MUTED;
    uint32_t wifi_color = app_wifi_got_ip() ? COL_OK : COL_MUTED;
    lv_obj_set_style_text_color(s_ble_badge, lv_color_hex(ble_color), 0);
    lv_obj_set_style_text_color(s_wifi_badge, lv_color_hex(wifi_color), 0);
}

static void render_home(void)
{
    lv_label_set_text(s_title, "Listen");
    const char *listen = app_listen_busy() ? "* 收音中..." : (s_nav.focus == 0 ? "* 开始收音" : "  开始收音");
    char body[512];
    snprintf(body, sizeof(body),
             "%s\n%s\n%s\n%s\n\n原文: %.80s\n译文: %.80s\n%.40s\nGrok: %s",
             listen,
             s_nav.focus == 1 ? "* 扬声器" : "  扬声器",
             s_nav.focus == 2 ? "* 存档" : "  存档",
             s_nav.focus == 3 ? "* 设置" : "  设置",
             s_source_line[0] ? s_source_line : "(无)",
             s_trans_line[0] ? s_trans_line : "(无)",
             s_status_note,
             app_xai_mode() == APP_XAI_MODE_LIVE ? "LIVE" : "MOCK");
    lv_label_set_text(s_body, body);
    lv_label_set_text(s_hint, "UP/DOWN  OK  长按返回");
}

static void render_speaker(void)
{
    lv_label_set_text(s_title, "扬声器");
    char ip[16] = { 0 };
    app_wifi_get_ip(ip, sizeof(ip));
    char body[288];
    snprintf(body, sizeof(body),
             "%s %s\n%s 音量 - (%u%%)\n%s 音量 +\n\nTCP %s:%u\n头: PASSPORT + PCM16\n%s",
             s_nav.focus == 0 ? "*" : " ",
             app_speaker_running() ? "停止接收" : "开始接收",
             s_nav.focus == 1 ? "*" : " ",
             (unsigned)app_speaker_volume(),
             s_nav.focus == 2 ? "*" : " ",
             ip[0] ? ip : "--",
             (unsigned)app_speaker_port(),
             app_speaker_playing() ? "播放中" : (app_speaker_running() ? "等待手机..." : "已停止"));
    lv_label_set_text(s_body, body);
    lv_label_set_text(s_hint, "长按 OK = 主页");
}

static void render_archive(void)
{
    lv_label_set_text(s_title, "存档");
    if (s_archive_open) {
        char src[APP_TEXT_MAX] = { 0 };
        char tr[APP_TEXT_MAX] = { 0 };
        (void)app_store_read(s_open_name, src, sizeof(src), tr, sizeof(tr));
        char body[480];
        snprintf(body, sizeof(body),
                 "%s\n\n原文:\n%.120s\n\n译文:\n%.120s\n\nOK=删除  长按=返回",
                 s_open_name, src[0] ? src : "(空)", tr[0] ? tr : "(空)");
        lv_label_set_text(s_body, body);
        lv_label_set_text(s_hint, "OK 删除");
        return;
    }
    if (s_archive_n <= 0) {
        lv_label_set_text(s_body, "* (空)\n\n收音成功后会写入 Flash。");
        lv_label_set_text(s_hint, "长按 OK = 主页");
        return;
    }
    char body[360];
    size_t o = 0;
    body[0] = '\0';
    int start = s_nav.focus > 2 ? s_nav.focus - 2 : 0;
    for (int i = start; i < s_archive_n && o + 40 < sizeof(body); i++) {
        o += (size_t)snprintf(body + o, sizeof(body) - o, "%s %s\n",
                              i == s_nav.focus ? "*" : " ", s_archive[i].name);
    }
    lv_label_set_text(s_body, body);
    lv_label_set_text(s_hint, "OK 打开  长按返回");
}

static void render_settings(void)
{
    lv_label_set_text(s_title, "设置");
    char ssid[33] = { 0 };
    char ip[16] = { 0 };
    app_wifi_get_ssid(ssid, sizeof(ssid));
    app_wifi_get_ip(ip, sizeof(ip));
    char body[360];
    snprintf(body, sizeof(body),
             "%s %s\n%s 清除 Wi-Fi\n%s 语言: %s\n%s Grok/xAI: %s (%s)\n\nSSID: %s\nIP: %s\nBLE: %s",
             s_nav.focus == 0 ? "*" : " ",
             app_blufi_is_active() ? "停止 BLUFI" : "启动 BLUFI",
             s_nav.focus == 1 ? "*" : " ",
             s_nav.focus == 2 ? "*" : " ",
             app_cloud_lang_label(),
             s_nav.focus == 3 ? "*" : " ",
             app_cloud_api_key_configured() ? "已配置" : "未配置",
             app_xai_mode() == APP_XAI_MODE_LIVE ? "LIVE" : "MOCK",
             ssid[0] ? ssid : "(无)",
             ip[0] ? ip : "--",
             APP_DEVICE_NAME);
    lv_label_set_text(s_body, body);
    lv_label_set_text(s_hint, "OK 执行  长按返回");
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

static void on_listen_done(const char *source, const char *translation,
                           bool mocked, esp_err_t err, void *user)
{
    (void)user;
    if (err == ESP_OK) {
        snprintf(s_source_line, sizeof(s_source_line), "%s", source ? source : "");
        snprintf(s_trans_line, sizeof(s_trans_line), "%s", translation ? translation : "");
        snprintf(s_status_note, sizeof(s_status_note), "%s", mocked ? "已保存 (MOCK)" : "已保存");
        refresh_archive_list();
        app_ble_set_status_text("idle");
    } else {
        snprintf(s_status_note, sizeof(s_status_note), "收音失败");
        app_ble_set_status_text("err");
    }
    s_ui_dirty = true;
}

static void on_activate(void)
{
    if (s_nav.page == APP_PAGE_HOME && s_nav.focus == 0) {
        if (app_listen_busy()) {
            app_listen_cancel();
            snprintf(s_status_note, sizeof(s_status_note), "取消中...");
            return;
        }
        if (app_speaker_running()) {
            snprintf(s_status_note, sizeof(s_status_note), "请先停扬声器");
            return;
        }
        snprintf(s_source_line, sizeof(s_source_line), "...");
        snprintf(s_trans_line, sizeof(s_trans_line), "...");
        snprintf(s_status_note, sizeof(s_status_note), "录音中...");
        app_ble_set_status_text("listen");
        esp_err_t err = app_listen_start(on_listen_done, NULL);
        if (err != ESP_OK) {
            snprintf(s_status_note, sizeof(s_status_note), "无法开始: %s", esp_err_to_name(err));
            app_ble_set_status_text("idle");
        }
        return;
    }

    if (s_nav.page == APP_PAGE_SPEAKER) {
        if (s_nav.focus == 0) {
            if (app_speaker_running()) {
                app_speaker_stop();
            } else {
                esp_err_t err = app_speaker_start();
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "speaker start: %s", esp_err_to_name(err));
                }
            }
        } else if (s_nav.focus == 1) {
            uint8_t v = app_speaker_volume();
            app_speaker_set_volume(v >= 10 ? (uint8_t)(v - 10) : 0);
        } else if (s_nav.focus == 2) {
            uint8_t v = app_speaker_volume();
            app_speaker_set_volume(v <= 90 ? (uint8_t)(v + 10) : 100);
        }
        return;
    }

    if (s_nav.page == APP_PAGE_ARCHIVE) {
        if (s_archive_open) {
            (void)app_store_delete(s_open_name);
            s_archive_open = false;
            s_open_name[0] = '\0';
            refresh_archive_list();
            return;
        }
        if (s_archive_n > 0 && s_nav.focus >= 0 && s_nav.focus < s_archive_n) {
            snprintf(s_open_name, sizeof(s_open_name), "%s", s_archive[s_nav.focus].name);
            s_archive_open = true;
        }
        return;
    }

    if (s_nav.page == APP_PAGE_SETTINGS) {
        if (s_nav.focus == 0) {
            if (app_blufi_is_active()) {
                app_blufi_stop();
                (void)app_ble_start();
            } else {
                if (app_speaker_running()) app_speaker_stop();
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
            ESP_LOGI(TAG, "xAI/Grok API key is %s (NVS cloud/xai_key; never commit)",
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

    if (nav_input == APP_NAV_INPUT_BACK && s_nav.page == APP_PAGE_ARCHIVE && s_archive_open) {
        s_archive_open = false;
        if (!bsp_lvgl_lock(200)) return;
        render_page();
        bsp_lvgl_unlock();
        return;
    }

    if (nav_input == APP_NAV_INPUT_BACK ||
        (nav_input != APP_NAV_INPUT_OK && s_nav.page == APP_PAGE_ARCHIVE && !s_archive_open)) {
        // entering archive refreshes list
    }
    if (s_nav.page == APP_PAGE_HOME && nav_input == APP_NAV_INPUT_OK && s_nav.focus == 2) {
        refresh_archive_list();
    }

    app_nav_result_t result = app_nav_handle(&s_nav, nav_input);
    if (result.action == APP_NAV_ACTION_ENTER_PAGE && result.page == APP_PAGE_ARCHIVE) {
        s_archive_open = false;
        refresh_archive_list();
    }
    if (result.action == APP_NAV_ACTION_ACTIVATE) on_activate();
    if (result.action == APP_NAV_ACTION_NONE && nav_input != APP_NAV_INPUT_OK) return;

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
    if (s_ui_dirty) {
        s_ui_dirty = false;
    }
    refresh_chrome();
    if (s_nav.page == APP_PAGE_SETTINGS || s_nav.page == APP_PAGE_HOME ||
        s_nav.page == APP_PAGE_SPEAKER || s_ui_dirty) {
        render_page();
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
    style_label(s_brand, font_ui(), COL_ACCENT);
    lv_label_set_text(s_brand, "PASSPORT");
    lv_obj_set_pos(s_brand, 8, 8);

    s_battery = lv_label_create(top);
    style_label(s_battery, font_ui(), COL_INK);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -8, 8);

    s_ble_badge = lv_label_create(s_scr);
    style_label(s_ble_badge, font_ui(), COL_MUTED);
    lv_obj_set_pos(s_ble_badge, 8, 42);

    s_wifi_badge = lv_label_create(s_scr);
    style_label(s_wifi_badge, font_ui(), COL_MUTED);
    lv_obj_set_pos(s_wifi_badge, 100, 42);

    s_title = lv_label_create(s_scr);
    style_label(s_title, font_ui(), COL_INK);
    lv_obj_set_pos(s_title, 8, 64);
    lv_obj_set_width(s_title, 224);

    lv_obj_t *panel = lv_obj_create(s_scr);
    lv_obj_set_size(panel, 224, 186);
    lv_obj_set_pos(panel, 8, 92);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    s_body = lv_label_create(panel);
    style_label(s_body, font_ui(), COL_INK);
    lv_obj_set_width(s_body, 200);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);

    s_hint = lv_label_create(s_scr);
    style_label(s_hint, font_ui(), COL_MUTED);
    lv_obj_set_pos(s_hint, 8, 286);
    lv_obj_set_width(s_hint, 224);

    s_source_line[0] = s_trans_line[0] = s_status_note[0] = '\0';
    app_nav_init(&s_nav);
    app_nav_set_speaker_count(&s_nav, 3);
    refresh_archive_list();
    render_page();
    lv_screen_load(s_scr);
    s_timer = lv_timer_create(tick, 400, NULL);
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
