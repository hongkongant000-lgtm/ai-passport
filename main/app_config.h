// Application configuration placeholders. Secrets live in NVS only — never commit API keys.
#pragma once

#include <stddef.h>

#define APP_DEVICE_NAME           "PassportTX"
#define APP_BLUFI_DEVICE_NAME     "BLUFI_PassportTX"

// NVS namespaces / keys for runtime credentials (values written on device or via phone).
#define APP_NVS_NS_CLOUD          "cloud"
#define APP_NVS_KEY_XAI_API       "xai_key"    // xAI / Grok API key; empty = unset
#define APP_NVS_KEY_LEGACY_GAPI   "gapi_key"   // legacy Google key name — ignored for live calls
#define APP_NVS_NS_LANG           "lang"
#define APP_NVS_KEY_SRC_LANG      "src"        // e.g. "zh-CN", "yue", "en", "auto"
#define APP_NVS_KEY_DST_LANG      "dst"        // e.g. "en"

#define APP_DEFAULT_SRC_LANG      "auto"
#define APP_DEFAULT_DST_LANG      "en"

// Custom Passport GATT (128-bit UUID base). Phone apps can discover/connect here.
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

// xAI / Grok HTTP APIs (Authorization: Bearer <key>; never log the key).
#define APP_XAI_STT_URL           "https://api.x.ai/v1/stt"
#define APP_XAI_CHAT_URL          "https://api.x.ai/v1/chat/completions"
#define APP_XAI_STT_MODEL         "grok-voice-transcribe-2.0"
#define APP_XAI_CHAT_MODEL        "grok-4-1-fast-non-reasoning"
