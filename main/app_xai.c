#include "app_xai.h"
#include "app_cloud.h"
#include "app_config.h"
#include "app_json.h"
#include "app_wifi.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_xai";

#define KEY_MAX 128
#define RESP_MAX 4096
#define HEAP_LIVE_MIN 100000
#define MP_BOUNDARY "passportxai7f3a"

app_xai_mode_t app_xai_mode(void)
{
    if (!app_cloud_api_key_configured()) return APP_XAI_MODE_MOCK;
    if (!app_wifi_got_ip()) return APP_XAI_MODE_MOCK;
    if (heap_caps_get_free_size(MALLOC_CAP_DEFAULT) < HEAP_LIVE_MIN) {
        return APP_XAI_MODE_MOCK;
    }
    return APP_XAI_MODE_LIVE;
}

static esp_err_t load_api_key(char *buf, size_t len)
{
    if (!buf || len == 0) return ESP_ERR_INVALID_ARG;
    buf[0] = '\0';
    nvs_handle_t h;
    esp_err_t err = nvs_open(APP_NVS_NS_CLOUD, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t need = len;
    err = nvs_get_str(h, APP_NVS_KEY_XAI_API, buf, &need);
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

static esp_err_t http_post(const char *url, const char *auth_bearer,
                           const char *content_type, const char *body, size_t body_len,
                           char *resp, size_t resp_len, int *status_out)
{
    if (resp && resp_len) resp[0] = '\0';
    resp_buf_t rb = { .buf = resp, .cap = resp_len, .len = 0 };
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event,
        .user_data = &rb,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;

    char auth[KEY_MAX + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", auth_bearer);
    esp_http_client_set_header(client, "Authorization", auth);
    wipe(auth, sizeof(auth));
    if (content_type) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }
    esp_http_client_set_post_field(client, body, (int)body_len);
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
    if (source && strstr(source, "hello")) {
        snprintf(translated, translated_len, "[mock] ni hao passport");
    } else if (source && strstr(source, "silence")) {
        snprintf(translated, translated_len, "[mock] (quiet)");
    } else {
        snprintf(translated, translated_len, "[tr] %s", source ? source : "");
    }
}

static void lang_for_stt(char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';
    char src[16] = { 0 };
    if (app_cloud_get_src_lang(src, sizeof(src)) != ESP_OK || src[0] == '\0' ||
        strcmp(src, "auto") == 0) {
        return; // omit / let server detect
    }
    if (strcmp(src, "zh-CN") == 0 || strcmp(src, "yue") == 0) {
        // yue → zh bias is approximate; Cantonese accuracy Unverified.
        snprintf(out, out_len, "zh");
        return;
    }
    if (strncmp(src, "en", 2) == 0) {
        snprintf(out, out_len, "en");
        return;
    }
    snprintf(out, out_len, "%.15s", src);
}

esp_err_t app_xai_stt(const int16_t *pcm, size_t samples,
                      char *text, size_t text_len, bool *used_mock)
{
    if (!text || text_len == 0) return ESP_ERR_INVALID_ARG;
    text[0] = '\0';
    if (used_mock) *used_mock = false;

    if (app_xai_mode() == APP_XAI_MODE_MOCK) {
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
    char lang[16];
    lang_for_stt(lang, sizeof(lang));
    char preamble[640];
    int pre_n;
    if (lang[0] != '\0') {
        pre_n = snprintf(preamble, sizeof(preamble),
                         "--" MP_BOUNDARY "\r\n"
                         "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                         "%s\r\n"
                         "--" MP_BOUNDARY "\r\n"
                         "Content-Disposition: form-data; name=\"audio_format\"\r\n\r\n"
                         "pcm\r\n"
                         "--" MP_BOUNDARY "\r\n"
                         "Content-Disposition: form-data; name=\"sample_rate\"\r\n\r\n"
                         "%d\r\n"
                         "--" MP_BOUNDARY "\r\n"
                         "Content-Disposition: form-data; name=\"language\"\r\n\r\n"
                         "%.15s\r\n"
                         "--" MP_BOUNDARY "\r\n"
                         "Content-Disposition: form-data; name=\"file\"; filename=\"clip.pcm\"\r\n"
                         "Content-Type: application/octet-stream\r\n\r\n",
                         APP_XAI_STT_MODEL, APP_AUDIO_HZ, lang);
    } else {
        pre_n = snprintf(preamble, sizeof(preamble),
                         "--" MP_BOUNDARY "\r\n"
                         "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                         "%s\r\n"
                         "--" MP_BOUNDARY "\r\n"
                         "Content-Disposition: form-data; name=\"audio_format\"\r\n\r\n"
                         "pcm\r\n"
                         "--" MP_BOUNDARY "\r\n"
                         "Content-Disposition: form-data; name=\"sample_rate\"\r\n\r\n"
                         "%d\r\n"
                         "--" MP_BOUNDARY "\r\n"
                         "Content-Disposition: form-data; name=\"file\"; filename=\"clip.pcm\"\r\n"
                         "Content-Type: application/octet-stream\r\n\r\n",
                         APP_XAI_STT_MODEL, APP_AUDIO_HZ);
    }
    if (pre_n <= 0 || (size_t)pre_n >= sizeof(preamble)) {
        wipe(key, sizeof(key));
        mock_stt(pcm, samples, text, text_len);
        if (used_mock) *used_mock = true;
        return ESP_OK;
    }

    static const char epilogue[] = "\r\n--" MP_BOUNDARY "--\r\n";
    size_t body_len = (size_t)pre_n + pcm_bytes + sizeof(epilogue) - 1;
    char *body = malloc(body_len);
    char *resp = malloc(RESP_MAX);
    if (!body || !resp) {
        free(body);
        free(resp);
        wipe(key, sizeof(key));
        mock_stt(pcm, samples, text, text_len);
        if (used_mock) *used_mock = true;
        return ESP_OK;
    }
    memcpy(body, preamble, (size_t)pre_n);
    memcpy(body + pre_n, pcm, pcm_bytes);
    memcpy(body + pre_n + pcm_bytes, epilogue, sizeof(epilogue) - 1);

    char ctype[80];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", MP_BOUNDARY);

    int status = 0;
    err = http_post(APP_XAI_STT_URL, key, ctype, body, body_len, resp, RESP_MAX, &status);
    if (err != ESP_OK || status < 200 || status >= 300 ||
        !app_json_extract_string(resp, "text", text, text_len) || text[0] == '\0') {
        ESP_LOGW(TAG, "STT live failed status=%d err=%s; mock fallback",
                 status, esp_err_to_name(err));
        mock_stt(pcm, samples, text, text_len);
        if (used_mock) *used_mock = true;
        err = ESP_OK;
    }

    free(body);
    free(resp);
    wipe(key, sizeof(key));
    return err;
}

esp_err_t app_xai_translate(const char *source,
                            char *translated, size_t translated_len,
                            bool *used_mock)
{
    if (!translated || translated_len == 0) return ESP_ERR_INVALID_ARG;
    translated[0] = '\0';
    if (used_mock) *used_mock = false;
    if (!source) source = "";

    if (app_xai_mode() == APP_XAI_MODE_MOCK) {
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

    char esc[APP_TEXT_MAX * 2];
    size_t eo = 0;
    for (size_t i = 0; source[i] && eo + 2 < sizeof(esc); i++) {
        if (source[i] == '"' || source[i] == '\\') esc[eo++] = '\\';
        esc[eo++] = source[i];
    }
    esc[eo] = '\0';

    const size_t body_cap = APP_TEXT_MAX * 2 + 512;
    char *body = malloc(body_cap);
    char *resp = malloc(RESP_MAX);
    if (!body || !resp) {
        free(body);
        free(resp);
        wipe(key, sizeof(key));
        mock_translate(source, translated, translated_len);
        if (used_mock) *used_mock = true;
        return ESP_OK;
    }

    snprintf(body, body_cap,
             "{\"model\":\"%s\",\"stream\":false,\"messages\":["
             "{\"role\":\"system\",\"content\":\"You are a translator. Translate the user text "
             "from source language '%s' to target language '%s'. "
             "Reply with ONLY the translated text, no quotes or commentary.\"},"
             "{\"role\":\"user\",\"content\":\"%.280s\"}]}",
             APP_XAI_CHAT_MODEL, src, dst, esc);

    int status = 0;
    err = http_post(APP_XAI_CHAT_URL, key, "application/json", body, strlen(body),
                    resp, RESP_MAX, &status);
    // Prefer message content; first "content" in chat response is the assistant text.
    if (err != ESP_OK || status < 200 || status >= 300 ||
        !app_json_extract_string(resp, "content", translated, translated_len) ||
        translated[0] == '\0') {
        ESP_LOGW(TAG, "translate live failed status=%d; mock fallback", status);
        mock_translate(source, translated, translated_len);
        if (used_mock) *used_mock = true;
        err = ESP_OK;
    }

    free(body);
    free(resp);
    wipe(key, sizeof(key));
    return err;
}
