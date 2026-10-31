#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

// xAI / Grok credential + language placeholders in NVS. Never log secret values.
esp_err_t app_cloud_init(void);
bool app_cloud_api_key_configured(void);
esp_err_t app_cloud_set_api_key(const char *key);  // empty clears
esp_err_t app_cloud_get_src_lang(char *buf, size_t len);
esp_err_t app_cloud_get_dst_lang(char *buf, size_t len);
esp_err_t app_cloud_set_lang_pair(const char *src, const char *dst);
void app_cloud_cycle_lang_pair(void);
const char *app_cloud_lang_label(void);
