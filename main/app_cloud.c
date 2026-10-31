#include "app_cloud.h"
#include "app_config.h"
#include "app_radio.h"

#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "app_cloud";

typedef struct {
    const char *src;
    const char *dst;
    const char *label;
} lang_pair_t;

static const lang_pair_t s_pairs[] = {
    { "auto", "en", "auto -> EN" },
    { "zh-CN", "en", "ZH -> EN" },
    { "yue", "en", "YUE -> EN" },
    { "en", "zh-CN", "EN -> ZH" },
};
static int s_pair_index;

esp_err_t app_cloud_init(void)
{
    esp_err_t err = app_radio_nvs_prepare();
    if (err != ESP_OK) return err;

    char src[16] = { 0 };
    char dst[16] = { 0 };
    if (app_cloud_get_src_lang(src, sizeof(src)) != ESP_OK || src[0] == '\0') {
        strncpy(src, APP_DEFAULT_SRC_LANG, sizeof(src) - 1);
    }
    if (app_cloud_get_dst_lang(dst, sizeof(dst)) != ESP_OK || dst[0] == '\0') {
        strncpy(dst, APP_DEFAULT_DST_LANG, sizeof(dst) - 1);
    }
    for (int i = 0; i < (int)(sizeof(s_pairs) / sizeof(s_pairs[0])); i++) {
        if (strcmp(src, s_pairs[i].src) == 0 && strcmp(dst, s_pairs[i].dst) == 0) {
            s_pair_index = i;
            break;
        }
    }
    ESP_LOGI(TAG, "cloud config ready; xai_key=%s lang=%s",
             app_cloud_api_key_configured() ? "set" : "unset",
             s_pairs[s_pair_index].label);
    return ESP_OK;
}

bool app_cloud_api_key_configured(void)
{
    nvs_handle_t h;
    if (nvs_open(APP_NVS_NS_CLOUD, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, APP_NVS_KEY_XAI_API, NULL, &len);
    nvs_close(h);
    return err == ESP_OK && len > 1;
}

esp_err_t app_cloud_set_api_key(const char *key)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(APP_NVS_NS_CLOUD, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (!key || key[0] == '\0') {
        err = nvs_erase_key(h, APP_NVS_KEY_XAI_API);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else {
        err = nvs_set_str(h, APP_NVS_KEY_XAI_API, key);
    }
    // Drop any leftover Google-era key so it cannot be mistaken for live config.
    (void)nvs_erase_key(h, APP_NVS_KEY_LEGACY_GAPI);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t get_lang(const char *key, char *buf, size_t len)
{
    if (!buf || len == 0) return ESP_ERR_INVALID_ARG;
    buf[0] = '\0';
    nvs_handle_t h;
    esp_err_t err = nvs_open(APP_NVS_NS_LANG, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t need = len;
    err = nvs_get_str(h, key, buf, &need);
    nvs_close(h);
    return err;
}

esp_err_t app_cloud_get_src_lang(char *buf, size_t len)
{
    return get_lang(APP_NVS_KEY_SRC_LANG, buf, len);
}

esp_err_t app_cloud_get_dst_lang(char *buf, size_t len)
{
    return get_lang(APP_NVS_KEY_DST_LANG, buf, len);
}

esp_err_t app_cloud_set_lang_pair(const char *src, const char *dst)
{
    if (!src || !dst) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(APP_NVS_NS_LANG, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, APP_NVS_KEY_SRC_LANG, src);
    if (err == ESP_OK) err = nvs_set_str(h, APP_NVS_KEY_DST_LANG, dst);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

void app_cloud_cycle_lang_pair(void)
{
    s_pair_index = (s_pair_index + 1) % (int)(sizeof(s_pairs) / sizeof(s_pairs[0]));
    (void)app_cloud_set_lang_pair(s_pairs[s_pair_index].src, s_pairs[s_pair_index].dst);
}

const char *app_cloud_lang_label(void)
{
    return s_pairs[s_pair_index].label;
}
