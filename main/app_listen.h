#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef void (*app_listen_done_cb_t)(const char *source, const char *translation,
                                     bool mocked, esp_err_t err, void *user);

// One-shot capture → STT → translate → optional SPIFFS save. Runs on a worker.
esp_err_t app_listen_start(app_listen_done_cb_t cb, void *user);
void app_listen_cancel(void);
bool app_listen_busy(void);
