#pragma once

#include "app_store_names.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[24];   // basename only, e.g. t0003.txt
    char path[48];   // full path under APP_STORAGE_BASE_PATH
} app_store_entry_t;

esp_err_t app_store_init(void);
int app_store_list(app_store_entry_t *entries, int max_entries);
esp_err_t app_store_save(const char *source, const char *translation,
                         char *out_name, size_t out_name_len);
esp_err_t app_store_read(const char *name, char *source, size_t source_len,
                         char *translation, size_t translation_len);
esp_err_t app_store_delete(const char *name);
uint32_t app_store_next_id(void);
