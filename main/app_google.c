#include "app_google.h"
#include "app_cloud.h"
#include "app_config.h"
#include "app_json.h"
#include "app_wifi.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_google";

#define KEY_MAX 96
#define RESP_MAX 4096
#define HEAP_LIVE_MIN 90000

app_google_mode_t app_google_mode(void)
{
    if (!app_cloud_api_key_configured()) return APP_GOOGLE_MODE_MOCK;
    if (!app_wifi_got_ip()) return APP_GOOGLE_MODE_MOCK;
    if (heap_caps_get_free_size(MALLOC_CAP_DEFAULT) < HEAP_LIVE_MIN) {
        return APP_GOOGLE_MODE_MOCK;
    }
    return APP_GOOGLE_MODE_LIVE;
}

static esp_err_t load_api_key(char *buf, size_t len)
{
    if (!buf || len == 0) return ESP_ERR_INVALID_ARG;
    buf[0] = '\0';
    nvs_handle_t h;
    esp_err_t err = nvs_open(APP_NVS_NS_CLOUD, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t need = len;
    err = nvs_get_str(h, APP_NVS_KEY_GOOGLE_API, buf, &need);
    nvs_close(h);
    return err;
}

static void wipe(char *buf, size_t len)
{
    if (buf) memset(buf, 0, len);
}

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} resp_buf_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    resp_buf_t *rb = evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !rb || !rb->buf) return ESP_OK;
    if (rb->len + (size_t)evt->data_len + 1 > rb->cap) return ESP_OK;
    memcpy(rb->buf + rb->len, evt->data, (size_t)evt->data_len);
    rb->len += (size_t)evt->data_len;
    rb->buf[rb->len] = '\0';
    return ESP_OK;
}

static esp_err_t http_post_json(const char *url, const char *body,
                                char *resp, size_t resp_len, int *status_out)
{
    if (resp && resp_len) resp[0] = '\0';
    resp_buf_t rb = { .buf = resp, .cap = resp_len, .len = 0 };
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event,
        .user_data = &rb,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));
    esp_err_t err = esp_http_client_perform(client);
    if (status_out) *status_out = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err;
}

static void mock_stt(const int16_t *pcm, size_t samples, char *text, size_t text_len)
{
    long energy = 0;
    size_t step = samples > 0 ? samples / 64 + 1 : 1;
    for (size_t i = 0; i < samples; i += step) {
        int v = pcm ? pcm[i] : 0;
        if (v < 0) v = -v;
        energy += v;
    }
    if (energy < 2000) {
        snprintf(text, text_len, "[mock] silence");
    } else {
        snprintf(text, text_len, "[mock] hello passport");
    }
}

static void mock_translate(const char *source, char *translated, size_t translated_len)
{
    char dst[16] = { 0 };
    if (app_cloud_get_dst_lang(dst, sizeof(dst)) != ESP_OK || dst[0] == '\0') {
        strncpy(dst, APP_DEFAULT_DST_LANG, sizeof(dst) - 1);
    }
    if (strcmp(dst, "zh-CN") == 0) {
        snprintf(translated, translated_len, "[tr] %s", source ? source : "");
    } else if (source && strstr(source, "hello")) {
        snprintf(translated, translated_len, "[mock] ni hao passport");
    } else if (source && strstr(source, "silence")) {
        snprintf(translated, translated_len, "[mock] (quiet)");
    } else {
        snprintf(translated, translated_len, "[tr] %s", source ? source : "");
    }
}

esp_err_t app_google_stt(const int16_t *pcm, size_t samples,
                         char *text, size_t text_len, bool *used_mock)
{
    if (!text || text_len == 0) return ESP_ERR_INVALID_ARG;
    text[0] = '\0';
    if (used_mock) *used_mock = false;

    if (app_google_mode() == APP_GOOGLE_MODE_MOCK) {
        mock_stt(pcm, samples, text, text_len);
        if (used_mock) *used_mock = true;
        return ESP_OK;
    }

    char key[KEY_MAX];
    esp_err_t err = load_api_key(key, sizeof(key));
    if (err != ESP_OK || key[0] == '\0') {
        mock_stt(pcm, samples, text, text_len);
        if (used_mock) *used_mock = true;
        wipe(key, sizeof(key));
        return ESP_OK;
    }

    size_t pcm_bytes = samples * sizeof(int16_t);
    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, (const unsigned char *)pcm, pcm_bytes);
    char *b64 = calloc(1, b64_len + 4);
    char *body = NULL;
    char *url = NULL;
    char *resp = NULL;
    if (!b64) {
        err = ESP_ERR_NO_MEM;
        goto done;
    }
    size_t written = 0;
    if (mbedtls_base64_encode((unsigned char *)b64, b64_len + 1, &written,
                              (const unsigned char *)pcm, pcm_bytes) != 0) {
        err = ESP_FAIL;
        goto done;
    }

    char src[16] = { 0 };
    if (app_cloud_get_src_lang(src, sizeof(src)) != ESP_OK || src[0] == '\0' ||
        strcmp(src, "auto") == 0) {
        strncpy(src, "en-US", sizeof(src) - 1);
    } else if (strcmp(src, "zh-CN") == 0) {
        strncpy(src, "zh-CN", sizeof(src) - 1);
    } else if (strcmp(src, "yue") == 0) {
        strncpy(src, "yue-Hant-HK", sizeof(src) - 1);
    }

    size_t body_cap = written + 256;
    body = malloc(body_cap);
    url = malloc(256);
    resp = malloc(RESP_MAX);
    if (!body || !url || !resp) {
        err = ESP_ERR_NO_MEM;
        goto done;
    }
    snprintf(body, body_cap,
             "{\"config\":{\"encoding\":\"LINEAR16\",\"sampleRateHertz\":%d,"
             "\"languageCode\":\"%s\",\"enableAutomaticPunctuation\":true},"
             "\"audio\":{\"content\":\"%s\"}}",
             APP_AUDIO_HZ, src, b64);
    snprintf(url, 256, "%s?key=%s", APP_GOOGLE_STT_URL, key);

    int status = 0;
    err = http_post_json(url, body, resp, RESP_MAX, &status);
    if (err != ESP_OK || status < 200 || status >= 300 ||
        !app_json_extract_string(resp, "transcript", text, text_len)) {
        ESP_LOGW(TAG, "STT live failed status=%d err=%s; falling back to mock",
                 status, esp_err_to_name(err));
        mock_stt(pcm, samples, text, text_len);
        if (used_mock) *used_mock = true;
        err = ESP_OK;
    }

done:
    free(b64);
    free(body);
    free(url);
    free(resp);
    wipe(key, sizeof(key));
    return err;
}

esp_err_t app_google_translate(const char *source,
                               char *translated, size_t translated_len,
                               bool *used_mock)
{
    if (!translated || translated_len == 0) return ESP_ERR_INVALID_ARG;
    translated[0] = '\0';
    if (used_mock) *used_mock = false;
    if (!source) source = "";

    if (app_google_mode() == APP_GOOGLE_MODE_MOCK) {
        mock_translate(source, translated, translated_len);
        if (used_mock) *used_mock = true;
        return ESP_OK;
    }

    char key[KEY_MAX];
    esp_err_t err = load_api_key(key, sizeof(key));
    if (err != ESP_OK || key[0] == '\0') {
        mock_translate(source, translated, translated_len);
        if (used_mock) *used_mock = true;
        wipe(key, sizeof(key));
        return ESP_OK;
    }

    char src[16] = { 0 };
    char dst[16] = { 0 };
    if (app_cloud_get_src_lang(src, sizeof(src)) != ESP_OK || src[0] == '\0') {
        strncpy(src, "auto", sizeof(src) - 1);
    }
    if (app_cloud_get_dst_lang(dst, sizeof(dst)) != ESP_OK || dst[0] == '\0') {
        strncpy(dst, APP_DEFAULT_DST_LANG, sizeof(dst) - 1);
    }

    const size_t body_cap = APP_TEXT_MAX * 2 + 192;
    char *body = malloc(body_cap);
    char *url = malloc(256);
    char *resp = malloc(RESP_MAX);
    if (!body || !url || !resp) {
        free(body); free(url); free(resp);
        wipe(key, sizeof(key));
        mock_translate(source, translated, translated_len);
        if (used_mock) *used_mock = true;
        return ESP_OK;
    }

    // Escape quotes in source for JSON (minimal).
    char esc[APP_TEXT_MAX * 2];
    size_t eo = 0;
    for (size_t i = 0; source[i] && eo + 2 < sizeof(esc); i++) {
        if (source[i] == '"' || source[i] == '\\') esc[eo++] = '\\';
        esc[eo++] = source[i];
    }
    esc[eo] = '\0';

    if (strcmp(src, "auto") == 0) {
        snprintf(body, body_cap,
                 "{\"q\":\"%.300s\",\"target\":\"%.15s\",\"format\":\"text\"}", esc, dst);
    } else {
        snprintf(body, body_cap,
                 "{\"q\":\"%.280s\",\"source\":\"%.15s\",\"target\":\"%.15s\",\"format\":\"text\"}",
                 esc, src, dst);
    }
    snprintf(url, 256, "%s?key=%s", APP_GOOGLE_TRANSLATE_URL, key);

    int status = 0;
    err = http_post_json(url, body, resp, RESP_MAX, &status);
    if (err != ESP_OK || status < 200 || status >= 300 ||
        !app_json_extract_string(resp, "translatedText", translated, translated_len)) {
        ESP_LOGW(TAG, "translate live failed status=%d; mock fallback", status);
        mock_translate(source, translated, translated_len);
        if (used_mock) *used_mock = true;
        err = ESP_OK;
    }

    free(body);
    free(url);
    free(resp);
    wipe(key, sizeof(key));
    return err;
}
