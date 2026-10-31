#pragma once

#include "esp_err.h"

// Ordered deep-sleep shutdown used by Settings → Sleep (and host contract tests).
esp_err_t app_power_enter_deep_sleep(void);
