#include "app_store.h"
#include "app_config.h"
#include "app_store_names.h"

#include "esp_log.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "app_store";
static bool s_ready;

esp_err_t app_store_init(void)
{
    // Mount is performed in main; here we only probe the path.
    struct stat st;
    if (stat(APP_STORAGE_BASE_PATH, &st) != 0) {
        ESP_LOGW(TAG, "storage path missing: %s", APP_STORAGE_BASE_PATH);
        s_ready = false;
        return ESP_ERR_NOT_FOUND;
    }
    s_ready = true;
    return ESP_OK;
}

static int cmp_entries(const void *a, const void *b)
{
    const app_store_entry_t *ea = a;
    const app_store_entry_t *eb = b;
    return strcmp(ea->name, eb->name);
}

int app_store_list(app_store_entry_t *entries, int max_entries)
{
    if (!entries || max_entries <= 0 || !s_ready) return 0;
    DIR *dir = opendir(APP_STORAGE_BASE_PATH);
    if (!dir) return 0;
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && n < max_entries) {
        if (!app_store_parse_id(ent->d_name, NULL)) continue;
        // Basename is constrained by app_store_parse_id (tNNNN.txt).
        strncpy(entries[n].name, ent->d_name, sizeof(entries[n].name) - 1);
        entries[n].name[sizeof(entries[n].name) - 1] = '\0';
        int path_n = snprintf(entries[n].path, sizeof(entries[n].path), "%s/%.23s",
                              APP_STORAGE_BASE_PATH, entries[n].name);
        if (path_n < 0 || (size_t)path_n >= sizeof(entries[n].path)) continue;
        n++;
    }
    closedir(dir);
    if (n > 1) qsort(entries, (size_t)n, sizeof(entries[0]), cmp_entries);
    return n;
}

uint32_t app_store_next_id(void)
{
    app_store_entry_t tmp[APP_STORE_MAX_ENTRIES];
    int n = app_store_list(tmp, APP_STORE_MAX_ENTRIES);
    uint32_t max_id = 0;
    for (int i = 0; i < n; i++) {
        uint32_t id = 0;
        if (app_store_parse_id(tmp[i].name, &id) && id > max_id) max_id = id;
    }
    return max_id + 1;
}

esp_err_t app_store_save(const char *source, const char *translation,
                         char *out_name, size_t out_name_len)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!source) source = "";
    if (!translation) translation = "";

    uint32_t id = app_store_next_id();
    if (id > 9999) return ESP_ERR_NO_MEM;
    char name[24];
    app_store_make_name(id, name, sizeof(name));
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", APP_STORAGE_BASE_PATH, name);

    FILE *f = fopen(path, "w");
    if (!f) return ESP_FAIL;
    fprintf(f, "SRC:%s\nTR:%s\n", source, translation);
    fclose(f);
    if (out_name && out_name_len) snprintf(out_name, out_name_len, "%s", name);
    ESP_LOGI(TAG, "saved %s", name);
    return ESP_OK;
}

esp_err_t app_store_read(const char *name, char *source, size_t source_len,
                         char *translation, size_t translation_len)
{
    if (!s_ready || !name) return ESP_ERR_INVALID_ARG;
    if (source && source_len) source[0] = '\0';
    if (translation && translation_len) translation[0] = '\0';

    char path[64];
    snprintf(path, sizeof(path), "%s/%s", APP_STORAGE_BASE_PATH, name);
    FILE *f = fopen(path, "r");
    if (!f) return ESP_ERR_NOT_FOUND;
    char line[APP_TEXT_MAX + 8];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (strncmp(line, "SRC:", 4) == 0 && source && source_len) {
            snprintf(source, source_len, "%s", line + 4);
        } else if (strncmp(line, "TR:", 3) == 0 && translation && translation_len) {
            snprintf(translation, translation_len, "%s", line + 3);
        }
    }
    fclose(f);
    return ESP_OK;
}

esp_err_t app_store_delete(const char *name)
{
    if (!s_ready || !name) return ESP_ERR_INVALID_ARG;
    if (!app_store_parse_id(name, NULL)) return ESP_ERR_INVALID_ARG;
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", APP_STORAGE_BASE_PATH, name);
    if (remove(path) != 0) return ESP_FAIL;
    ESP_LOGI(TAG, "deleted %s", name);
    return ESP_OK;
}
