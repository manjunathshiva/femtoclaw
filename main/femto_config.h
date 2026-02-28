#pragma once

/* FemtoClaw Global Configuration */

/* ── PSRAM-aware allocation ──────────────────────────────────── */
/* CONFIG_SPIRAM: set on ESP32-S3 (8MB PSRAM), unset on ESP-WROOM-32 (no PSRAM).
 * femto_alloc/femto_realloc route to PSRAM when available, else standard heap. */
#include "sdkconfig.h"
#include <stdlib.h>
#if CONFIG_SPIRAM
#include "esp_heap_caps.h"
#define femto_alloc(size)    heap_caps_calloc(1, (size), MALLOC_CAP_SPIRAM)
#define femto_realloc(p, s)  heap_caps_realloc((p), (s), MALLOC_CAP_SPIRAM)
#else
#define femto_alloc(size)    calloc(1, (size))
#define femto_realloc(p, s)  realloc((p), (s))
#endif

/* Build-time secrets (highest priority, override NVS) */
#if __has_include("femto_secrets.h")
#include "femto_secrets.h"
#endif

#ifndef FEMTO_SECRET_WIFI_SSID
#define FEMTO_SECRET_WIFI_SSID       ""
#endif
#ifndef FEMTO_SECRET_WIFI_PASS
#define FEMTO_SECRET_WIFI_PASS       ""
#endif
#ifndef FEMTO_SECRET_TG_TOKEN
#define FEMTO_SECRET_TG_TOKEN        ""
#endif
#ifndef FEMTO_SECRET_API_KEY
#define FEMTO_SECRET_API_KEY         ""
#endif
#ifndef FEMTO_SECRET_MODEL
#define FEMTO_SECRET_MODEL           ""
#endif
#ifndef FEMTO_SECRET_MODEL_PROVIDER
#define FEMTO_SECRET_MODEL_PROVIDER  "anthropic"
#endif
#ifndef FEMTO_SECRET_API_BASE_URL
#define FEMTO_SECRET_API_BASE_URL    ""
#endif
#ifndef FEMTO_SECRET_PROXY_HOST
#define FEMTO_SECRET_PROXY_HOST      ""
#endif
#ifndef FEMTO_SECRET_PROXY_PORT
#define FEMTO_SECRET_PROXY_PORT      ""
#endif
#ifndef FEMTO_SECRET_SEARCH_KEY
#define FEMTO_SECRET_SEARCH_KEY      ""
#endif
#ifndef FEMTO_SECRET_TIMEZONE
#define FEMTO_SECRET_TIMEZONE        ""
#endif

/* WiFi */
#define FEMTO_WIFI_MAX_RETRY          10
#define FEMTO_WIFI_RETRY_BASE_MS      1000
#define FEMTO_WIFI_RETRY_MAX_MS       30000

/* Telegram Bot */
#define FEMTO_TG_POLL_TIMEOUT_S       30
#define FEMTO_TG_MAX_MSG_LEN          4096
#define FEMTO_TG_POLL_STACK           (12 * 1024)
#define FEMTO_TG_POLL_PRIO            5
#define FEMTO_TG_POLL_CORE            0
#define FEMTO_TG_CARD_SHOW_MS         3000
#define FEMTO_TG_CARD_BODY_SCALE      3

/* Agent Loop */
#define FEMTO_AGENT_STACK             (24 * 1024)
#define FEMTO_AGENT_PRIO              6
#define FEMTO_AGENT_CORE              1
#define FEMTO_AGENT_MAX_HISTORY       20
#define FEMTO_AGENT_MAX_TOOL_ITER     10
#define FEMTO_MAX_TOOL_CALLS          4
#define FEMTO_AGENT_SEND_WORKING_STATUS 1

/* Timezone (POSIX TZ format) */
#define FEMTO_TIMEZONE_DEFAULT        "PST8PDT,M3.2.0,M11.1.0"
#define FEMTO_TIMEZONE                (FEMTO_SECRET_TIMEZONE[0] ? FEMTO_SECRET_TIMEZONE : FEMTO_TIMEZONE_DEFAULT)

/* LLM */
#define FEMTO_LLM_DEFAULT_MODEL       "claude-opus-4-5"
#define FEMTO_LLM_PROVIDER_DEFAULT    "anthropic"
#define FEMTO_LLM_MAX_TOKENS          4096
#define FEMTO_LLM_API_URL             "https://api.anthropic.com/v1/messages"
#define FEMTO_OPENAI_API_URL          "https://api.openai.com/v1/chat/completions"
#define FEMTO_LLM_API_VERSION         "2023-06-01"
#if CONFIG_SPIRAM
#define FEMTO_LLM_STREAM_BUF_SIZE     (32 * 1024)
#else
#define FEMTO_LLM_STREAM_BUF_SIZE     (8 * 1024)
#endif
#define FEMTO_LLM_LOG_VERBOSE_PAYLOAD 0
#define FEMTO_LLM_LOG_PREVIEW_BYTES   160

/* Message Bus */
#define FEMTO_BUS_QUEUE_LEN           16
#define FEMTO_OUTBOUND_STACK          (12 * 1024)
#define FEMTO_OUTBOUND_PRIO           5
#define FEMTO_OUTBOUND_CORE           0

/* Memory / SPIFFS */
#define FEMTO_SPIFFS_BASE             "/spiffs"
#define FEMTO_SPIFFS_CONFIG_DIR       "/spiffs/config"
#define FEMTO_SPIFFS_MEMORY_DIR       "/spiffs/memory"
#define FEMTO_SPIFFS_SESSION_DIR      "/spiffs/sessions"
#define FEMTO_MEMORY_FILE             "/spiffs/memory/MEMORY.md"
#define FEMTO_SOUL_FILE               "/spiffs/config/SOUL.md"
#define FEMTO_USER_FILE               "/spiffs/config/USER.md"
#if CONFIG_SPIRAM
#define FEMTO_CONTEXT_BUF_SIZE        (16 * 1024)
#else
#define FEMTO_CONTEXT_BUF_SIZE        (4 * 1024)
#endif
#define FEMTO_SESSION_MAX_MSGS        20

/* Cron / Heartbeat */
#define FEMTO_CRON_FILE               "/spiffs/cron.json"
#define FEMTO_CRON_MAX_JOBS           16
#define FEMTO_CRON_CHECK_INTERVAL_MS  (60 * 1000)
#define FEMTO_HEARTBEAT_FILE          "/spiffs/HEARTBEAT.md"
#define FEMTO_HEARTBEAT_INTERVAL_MS   (30 * 60 * 1000)

/* Skills */
#define FEMTO_SKILLS_PREFIX           "/spiffs/skills/"

/* WebSocket Gateway */
#define FEMTO_WS_PORT                 18789
#define FEMTO_WS_MAX_CLIENTS          4

/* Serial CLI */
#define FEMTO_CLI_STACK               (4 * 1024)
#define FEMTO_CLI_PRIO                3
#define FEMTO_CLI_CORE                0

/* NVS Namespaces */
#define FEMTO_NVS_WIFI                "wifi_config"
#define FEMTO_NVS_TG                  "tg_config"
#define FEMTO_NVS_LLM                 "llm_config"
#define FEMTO_NVS_PROXY               "proxy_config"
#define FEMTO_NVS_SEARCH              "search_config"

/* NVS Keys */
#define FEMTO_NVS_KEY_SSID            "ssid"
#define FEMTO_NVS_KEY_PASS            "password"
#define FEMTO_NVS_KEY_TG_TOKEN        "bot_token"
#define FEMTO_NVS_KEY_API_KEY         "api_key"
#define FEMTO_NVS_KEY_MODEL           "model"
#define FEMTO_NVS_KEY_PROVIDER        "provider"
#define FEMTO_NVS_KEY_API_BASE_URL    "api_base_url"
#define FEMTO_NVS_KEY_PROXY_HOST      "host"
#define FEMTO_NVS_KEY_PROXY_PORT      "port"
