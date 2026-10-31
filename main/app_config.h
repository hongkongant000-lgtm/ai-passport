// Application configuration placeholders. Secrets live in NVS only — never commit API keys.
#pragma once

#include <stddef.h>

#define APP_DEVICE_NAME           "PassportTX"
#define APP_BLUFI_DEVICE_NAME     "BLUFI_PassportTX"

// NVS namespaces / keys for runtime credentials (values written on device or via phone).
#define APP_NVS_NS_CLOUD          "cloud"
#define APP_NVS_KEY_GOOGLE_API    "gapi_key"   // Google API key string; empty = unset
#define APP_NVS_NS_LANG           "lang"
#define APP_NVS_KEY_SRC_LANG      "src"        // e.g. "zh-CN", "yue", "en", "auto"
#define APP_NVS_KEY_DST_LANG      "dst"        // e.g. "en"

#define APP_DEFAULT_SRC_LANG      "auto"
#define APP_DEFAULT_DST_LANG      "en"

// Custom Passport GATT (128-bit UUID base). Phone apps can discover/connect here.
// 6b5c0001-7a3e-4f91-9c2d-1e8f0a4b6d70 and +1/+2 characteristics.
#define APP_GATT_SVC_UUID128 \
    0x70, 0x6d, 0x4b, 0x0a, 0x8f, 0x1e, 0x2d, 0x9c, \
    0x91, 0x4f, 0x3e, 0x7a, 0x01, 0x00, 0x5c, 0x6b
#define APP_GATT_CHR_STATUS_UUID128 \
    0x70, 0x6d, 0x4b, 0x0a, 0x8f, 0x1e, 0x2d, 0x9c, \
    0x91, 0x4f, 0x3e, 0x7a, 0x02, 0x00, 0x5c, 0x6b
#define APP_GATT_CHR_CTRL_UUID128 \
    0x70, 0x6d, 0x4b, 0x0a, 0x8f, 0x1e, 0x2d, 0x9c, \
    0x91, 0x4f, 0x3e, 0x7a, 0x03, 0x00, 0x5c, 0x6b

// SPIFFS mount for saved transcripts (partition label must match partitions.csv).
#define APP_STORAGE_PARTITION     "storage"
#define APP_STORAGE_BASE_PATH     "/store"

// Audio / cloud budgets (no PSRAM — keep clips short).
#define APP_AUDIO_HZ              16000
#define APP_LISTEN_MS             750
#define APP_LISTEN_MAX_SAMPLES    ((APP_AUDIO_HZ * APP_LISTEN_MS) / 1000)
#define APP_SPEAKER_TCP_PORT      9090
#define APP_STORE_MAX_ENTRIES     24
#define APP_TEXT_MAX              160

// Google endpoints (API key appended as query param; never logged).
#define APP_GOOGLE_STT_URL \
    "https://speech.googleapis.com/v1/speech:recognize"
#define APP_GOOGLE_TRANSLATE_URL \
    "https://translation.googleapis.com/language/translate/v2"
