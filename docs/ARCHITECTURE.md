# FemtoClaw Architecture

> ESP32 / ESP32-S3 AI Agent firmware — C/FreeRTOS implementation running on bare metal (no Linux).

---

## System Overview

```
Telegram App (User)
    │
    │  HTTPS Long Polling
    │
    ▼
┌──────────────────────────────────────────────────┐
│          ESP32 / ESP32-S3 (FemtoClaw)            │
│                                                  │
│   ┌─────────────┐       ┌──────────────────┐     │
│   │  Telegram    │──────▶│   Inbound Queue  │     │
│   │  Poller      │       └────────┬─────────┘     │
│   │  (Core 0)    │               │                │
│   └─────────────┘               ▼                │
│                     ┌────────────────────────┐    │
│   ┌─────────────┐  │     Agent Loop          │    │
│   │  WebSocket   │─▶│     (Core 1)           │    │
│   │  Server      │  │                        │    │
│   │  (:18789)    │  │  Context ──▶ LLM Proxy │    │
│   └─────────────┘  │  Builder   (multi-prov) │    │
│                     │       ▲          │      │    │
│   ┌─────────────┐  │       │     tool_use?   │    │
│   │  Serial CLI  │  │       │          ▼      │    │
│   │  (Core 0)    │  │  Tool Results ◀─ Tools  │    │
│   └─────────────┘  │              (web_search)│    │
│                     └──────────┬─────────────┘    │
│                                │                  │
│                         ┌──────▼───────┐          │
│                         │ Outbound Queue│          │
│                         └──────┬───────┘          │
│                                │                  │
│                         ┌──────▼───────┐          │
│                         │  Outbound    │          │
│                         │  Dispatch    │          │
│                         │  (Core 0)    │          │
│                         └──┬────────┬──┘          │
│                            │        │             │
│                     Telegram    WebSocket          │
│                     sendMessage  send              │
│                                                   │
│   ┌──────────────────────────────────────────┐    │
│   │  SPIFFS (flash-dependent)                │    │
│   │  /spiffs/config/  SOUL.md, USER.md       │    │
│   │  /spiffs/memory/  MEMORY.md, YYYY-MM-DD  │    │
│   │  /spiffs/sessions/ tg_<chat_id>.jsonl    │    │
│   └──────────────────────────────────────────┘    │
└───────────────────────────────────────────────────┘
         │
         │  LLM API (HTTPS or HTTP)
         │  + Web Search (HTTPS)
         ▼
   ┌───────────┐ ┌──────────┐ ┌────────┐ ┌────────┐
   │ Claude API │ │ OpenAI   │ │ Ollama │ │LMStudio│
   └───────────┘ └──────────┘ └────────┘ └────────┘
   ┌───────────────┐  ┌──────────────────┐
   │ Brave Search  │  │ DuckDuckGo (DDG) │
   │ (API key)     │  │ (zero-config)    │
   └───────────────┘  └──────────────────┘
```

---

## Data Flow

```
1. User sends message on Telegram (or WebSocket)
2. Channel poller receives message, wraps in femto_msg_t
3. Message pushed to Inbound Queue (FreeRTOS xQueue)
4. Agent Loop (Core 1) pops message:
   a. Load session history from SPIFFS (JSONL)
   b. Build system prompt (SOUL.md + USER.md + MEMORY.md + recent notes + tool guidance)
   c. Build cJSON messages array (history + current message)
   d. ReAct loop (max 10 iterations):
      i.   Call LLM via LLM Proxy (Anthropic, OpenAI, Ollama, or LM Studio)
      ii.  Parse JSON response → text blocks + tool_use blocks
      iii. If stop_reason == "tool_use":
           - Execute each tool (e.g. web_search → Brave or DuckDuckGo)
           - Append assistant content + tool_result to messages
           - Continue loop
      iv.  If stop_reason == "end_turn": break with final text
   e. Save user message + final assistant text to session file
   f. Push response to Outbound Queue
5. Outbound Dispatch (Core 0) pops response:
   a. Route by channel field ("telegram" → sendMessage, "websocket" → WS frame)
6. User receives reply
```

---

## Module Map

```
main/
├── femto.c                  Entry point — app_main() orchestrates init + startup
├── femto_config.h           All compile-time constants + build-time secrets include
├── femto_secrets.h          Build-time credentials (gitignored, highest priority)
├── femto_secrets.h.example  Template for femto_secrets.h
│
├── bus/
│   ├── message_bus.h       femto_msg_t struct, queue API
│   └── message_bus.c       Two FreeRTOS queues: inbound + outbound
│
├── wifi/
│   ├── wifi_manager.h      WiFi STA lifecycle API
│   └── wifi_manager.c      Event handler, exponential backoff
│
├── telegram/
│   ├── telegram_bot.h      Bot init/start, send_message API
│   └── telegram_bot.c      Long polling loop, JSON parsing, message splitting
│
├── llm/
│   ├── llm_proxy.h         llm_chat() + llm_chat_tools() API, tool_use types
│   └── llm_proxy.c         Multi-provider LLM (Anthropic, OpenAI, Ollama, LM Studio)
│
├── agent/
│   ├── agent_loop.h        Agent task init/start
│   ├── agent_loop.c        ReAct loop: LLM call → tool execution → repeat
│   ├── context_builder.h   System prompt + messages builder API
│   └── context_builder.c   Reads bootstrap files + memory + tool guidance
│
├── tools/
│   ├── tool_registry.h     Tool definition struct, register/dispatch API
│   ├── tool_registry.c     Tool registration, JSON schema builder, dispatch by name
│   ├── tool_web_search.h   Web search tool API
│   ├── tool_web_search.c   Brave Search API or DuckDuckGo fallback (direct + proxy)
│   ├── tool_gpio.h         GPIO control tool API
│   └── tool_gpio.c         GPIO set/read/blink/sequence with safe pin allowlists
│
├── memory/
│   ├── memory_store.h      Long-term + daily memory API
│   ├── memory_store.c      MEMORY.md read/write, daily .md append/read
│   ├── session_mgr.h       Per-chat session API
│   └── session_mgr.c       JSONL session files, ring buffer history
│
├── gateway/
│   ├── ws_server.h         WebSocket server API
│   └── ws_server.c         ESP HTTP server with WS upgrade, client tracking
│
├── proxy/
│   ├── http_proxy.h        Proxy connection API
│   └── http_proxy.c        HTTP CONNECT tunnel + TLS via esp_tls
│
├── cli/
│   ├── serial_cli.h        CLI init API
│   └── serial_cli.c        esp_console REPL with debug/maintenance commands
│
└── ota/
    ├── ota_manager.h       OTA update API
    └── ota_manager.c       HTTP/HTTPS OTA via esp_https_ota (+ HTTP endpoint on WS server)
```

---

## FreeRTOS Task Layout

| Task               | Core | Priority | Stack  | Description                          |
|--------------------|------|----------|--------|--------------------------------------|
| `tg_poll`          | 0    | 5        | 12 KB  | Telegram long polling (30s timeout)  |
| `agent_loop`       | 1    | 6        | 12 KB  | Message processing + LLM API call    |
| `outbound`         | 0    | 5        | 8 KB   | Route responses to Telegram / WS     |
| `serial_cli`       | 0    | 3        | 4 KB   | USB serial console REPL              |
| httpd (internal)   | 0    | 5        | —      | WebSocket server (esp_http_server)   |
| wifi_event (IDF)   | 0    | 8        | —      | WiFi event handling (ESP-IDF)        |

**Core allocation strategy**: Core 0 handles I/O (network, serial, WiFi). Core 1 is dedicated to the agent loop (CPU-bound JSON building + waiting on HTTPS).

---

## Memory Budget

### ESP32-S3 (8 MB PSRAM)

| Purpose                            | Location       | Size     |
|------------------------------------|----------------|----------|
| FreeRTOS task stacks               | Internal SRAM  | ~40 KB   |
| WiFi buffers                       | Internal SRAM  | ~30 KB   |
| TLS connections x2 (Telegram + LLM)| PSRAM          | ~120 KB  |
| JSON parse buffers                 | PSRAM          | ~32 KB   |
| Session history cache              | PSRAM          | ~32 KB   |
| System prompt buffer               | PSRAM          | ~16 KB   |
| LLM response stream buffer         | PSRAM          | ~32 KB   |
| Remaining available                | PSRAM          | ~7.7 MB  |

Large buffers (32 KB+) are allocated from PSRAM via `heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM)`.

### ESP-WROOM-32 (no PSRAM)

| Purpose                            | Location       | Size     |
|------------------------------------|----------------|----------|
| FreeRTOS task stacks               | Internal SRAM  | ~40 KB   |
| WiFi buffers                       | Internal SRAM  | ~30 KB   |
| TLS connections x2 (Telegram + LLM)| Internal SRAM  | ~120 KB  |
| System prompt buffer               | Internal SRAM  | ~4 KB    |
| LLM response stream buffer         | Internal SRAM  | ~8 KB    |
| Remaining available                | Internal SRAM  | Very tight |

On ESP-WROOM-32, all buffers use standard `calloc()`. Buffer sizes are reduced via `#if CONFIG_SPIRAM` guards in `femto_config.h` (e.g. stream buffer 8 KB vs 32 KB, context buffer 4 KB vs 16 KB). There is no headroom for a 3rd TLS connection.

---

## Flash Partition Layout

```
Offset      Size      Name        Purpose
─────────────────────────────────────────────
0x009000    24 KB     nvs         ESP-IDF internal use (WiFi calibration etc.)
0x00F000     8 KB     otadata     OTA boot state
0x011000     4 KB     phy_init    WiFi PHY calibration
0x020000     2 MB     ota_0       Firmware slot A
0x220000     2 MB     ota_1       Firmware slot B
0x420000    12 MB     spiffs      Markdown memory, sessions, config
0xFF0000    64 KB     coredump    Crash dump storage
```

Total: 16 MB flash.

---

## Storage Layout (SPIFFS)

SPIFFS is a flat filesystem — no real directories. Files use path-like names.

```
/spiffs/config/SOUL.md          AI personality definition
/spiffs/config/USER.md          User profile
/spiffs/memory/MEMORY.md        Long-term persistent memory
/spiffs/memory/2026-02-05.md    Daily notes (one file per day)
/spiffs/sessions/tg_12345.jsonl Session history (one file per Telegram chat)
```

Session files are JSONL (one JSON object per line):
```json
{"role":"user","content":"Hello","ts":1738764800}
{"role":"assistant","content":"Hi there!","ts":1738764802}
```

---

## Configuration

Configuration uses a two-layer priority system: **NVS (runtime CLI) > build-time secrets > defaults**.

### Build-time secrets (`femto_secrets.h`)

| Define                          | Description                                          |
|---------------------------------|------------------------------------------------------|
| `FEMTO_SECRET_WIFI_SSID`        | WiFi SSID                                            |
| `FEMTO_SECRET_WIFI_PASS`        | WiFi password                                        |
| `FEMTO_SECRET_TG_TOKEN`         | Telegram Bot API token                               |
| `FEMTO_SECRET_API_KEY`          | LLM API key (Anthropic, OpenAI, or local)            |
| `FEMTO_SECRET_MODEL`            | Model ID (default: `claude-opus-4-5`)                |
| `FEMTO_SECRET_MODEL_PROVIDER`   | `"anthropic"` or `"openai"` (Ollama/LM Studio use `"openai"`) |
| `FEMTO_SECRET_API_BASE_URL`     | Custom endpoint URL (e.g. `http://192.168.1.100:1234` for LM Studio, `http://192.168.1.100:11434` for Ollama) |
| `FEMTO_SECRET_PROXY_HOST`       | HTTP proxy hostname/IP (optional)                    |
| `FEMTO_SECRET_PROXY_PORT`       | HTTP proxy port (optional)                           |
| `FEMTO_SECRET_SEARCH_KEY`       | Brave Search API key (optional, DuckDuckGo used if empty) |
| `FEMTO_SECRET_TIMEZONE`         | POSIX TZ string (optional, e.g. `"IST-5:30"`)       |

### NVS runtime overrides (via Serial CLI)

All build-time secrets can be overridden at runtime via CLI commands. NVS values take highest priority and persist across reboots.

---

## Message Bus Protocol

The internal message bus uses two FreeRTOS queues carrying `femto_msg_t`:

```c
typedef struct {
    char channel[16];   // "telegram", "websocket", "cli"
    char chat_id[32];   // Telegram chat ID or WS client ID
    char *content;      // Heap-allocated text (ownership transferred)
} femto_msg_t;
```

- **Inbound queue**: channels → agent loop (depth: 8)
- **Outbound queue**: agent loop → dispatch → channels (depth: 8)
- Content string ownership is transferred on push; receiver must `free()`.

---

## WebSocket Protocol

Port: **18789**. Max clients: **4**.

**Client → Server:**
```json
{"type": "message", "content": "Hello", "chat_id": "ws_client1"}
```

**Server → Client:**
```json
{"type": "response", "content": "Hi there!", "chat_id": "ws_client1"}
```

Client `chat_id` is auto-assigned on connection (`ws_<fd>`) but can be overridden in the first message.

---

## LLM Provider Integration

FemtoClaw supports multiple LLM backends through `llm_proxy.c`. The provider is selected via `s_provider` (`"anthropic"` or `"openai"`), and the endpoint URL is determined by `s_api_base_url`.

### URL Routing

| Provider | Custom base URL set? | Endpoint used |
|----------|---------------------|---------------|
| `anthropic` | No | `https://api.anthropic.com/v1/messages` |
| `anthropic` | Yes | `<base_url>/v1/messages` |
| `openai` | No | `https://api.openai.com/v1/chat/completions` |
| `openai` | Yes | `<base_url>/v1/chat/completions` |

**Ollama** and **LM Studio** use provider `"openai"` with a custom base URL (e.g. `http://192.168.1.100:11434` or `http://192.168.1.100:1234`). The proxy auto-appends the `/v1/chat/completions` path.

### HTTP Dispatch

Two code paths handle the HTTP request:

- **Direct path** (`llm_http_direct()`): Uses `esp_http_client` directly. Supports both HTTP and HTTPS. Default for local endpoints (Ollama, LM Studio) and cloud APIs without a proxy.
- **Proxy path** (`llm_http_via_proxy()`): Manual HTTP over CONNECT tunnel via `http_proxy.c`. Only used when a proxy is configured AND the endpoint is HTTPS.

### Authentication Headers

| Provider | Headers |
|----------|---------|
| `anthropic` | `x-api-key: <key>`, `anthropic-version: 2023-06-01` |
| `openai` | `Authorization: Bearer <key>` |

### Request Format — Anthropic

```json
{
  "model": "claude-opus-4-5",
  "max_tokens": 4096,
  "system": "<system prompt>",
  "tools": [{"name": "web_search", "description": "...", "input_schema": {...}}],
  "messages": [{"role": "user", "content": "Hello"}]
}
```

Key difference: `system` is a top-level field, not inside the `messages` array.

### Request Format — OpenAI (also Ollama / LM Studio)

```json
{
  "model": "gpt-4o",
  "max_completion_tokens": 4096,
  "messages": [
    {"role": "system", "content": "<system prompt>"},
    {"role": "user", "content": "Hello"}
  ],
  "tools": [{"type": "function", "function": {"name": "web_search", "description": "...", "parameters": {...}}}]
}
```

The proxy converts Anthropic-style tool schemas to OpenAI function-calling format automatically.

### Response Handling

The agent loop checks `stop_reason` (Anthropic) or `finish_reason` (OpenAI) after each call:
- `"tool_use"` / `"tool_calls"` → execute tools, append results, continue loop
- `"end_turn"` / `"stop"` → break with final text

The loop repeats until the LLM stops calling tools (max 10 iterations).

---

## Web Search

The `web_search` tool supports two backends, selected automatically based on whether a Brave Search API key is configured.

### Brave Search (API key required)

- **Endpoint**: `GET https://api.search.brave.com/res/v1/web/search?q=<query>&count=5`
- **Auth header**: `X-Subscription-Token: <key>`
- **Response**: JSON — parses `web.results[].{title, url, description}`
- Supports direct HTTPS and proxy path

### DuckDuckGo (zero-config fallback)

- **Endpoint**: `POST https://html.duckduckgo.com/html/`
- **No API key required** — works out of the box
- **Response**: HTML — custom parser extracts results from `class="result__a"` (title), `href` (URL), `class="result__snippet"` (description)
- Smart header stripping: skips ~8KB of DDG boilerplate before `class="results"` marker, allowing 5 results to fit in the 8KB buffer on non-PSRAM devices
- URL decoding: unwraps DDG redirect URLs (`//duckduckgo.com/l/?uddg=<encoded>&...`)

---

## Startup Sequence

```
app_main()
  ├── init_nvs()                    NVS flash init (erase if corrupted)
  ├── esp_event_loop_create_default()
  ├── init_spiffs()                 Mount SPIFFS at /spiffs
  ├── message_bus_init()            Create inbound + outbound queues
  ├── memory_store_init()           Verify SPIFFS paths
  ├── session_mgr_init()
  ├── wifi_manager_init()           Init WiFi STA mode + event handlers
  ├── http_proxy_init()             Load proxy config from build-time secrets
  ├── telegram_bot_init()           Load bot token from build-time secrets
  ├── llm_proxy_init()              Load API key + model from build-time secrets
  ├── tool_registry_init()          Register tools, build tools JSON
  ├── agent_loop_init()
  ├── serial_cli_init()             Start REPL (works without WiFi)
  │
  ├── wifi_manager_start()          Connect using build-time credentials
  │   └── wifi_manager_wait_connected(30s)
  │
  └── [if WiFi connected]
      ├── telegram_bot_start()      Launch tg_poll task (Core 0)
      ├── agent_loop_start()        Launch agent_loop task (Core 1)
      ├── ws_server_start()         Start httpd on port 18789
      └── outbound_dispatch task    Launch outbound task (Core 0)
```

If WiFi credentials are missing or connection times out, the CLI remains available for diagnostics.

---

## Serial CLI Commands

The CLI provides runtime configuration (saved to NVS, overrides build-time defaults) and debug/maintenance commands.

**Runtime config** (persisted to NVS):

| Command                              | Description                                      |
|--------------------------------------|--------------------------------------------------|
| `set_wifi <SSID> <PASS>`             | Change WiFi credentials                          |
| `set_tg_token <TOKEN>`               | Change Telegram bot token                        |
| `set_api_key <KEY>`                  | Change LLM API key                               |
| `set_model_provider <PROVIDER>`      | Switch provider (`anthropic` or `openai`)        |
| `set_model <MODEL>`                  | Change model ID                                  |
| `set_api_base_url <URL>`             | Set custom LLM endpoint (Ollama, LM Studio)      |
| `clear_api_base_url`                 | Revert to default provider URL                   |
| `set_proxy <HOST> <PORT>`            | Set HTTP CONNECT proxy                           |
| `clear_proxy`                        | Remove proxy                                     |
| `set_search_key <KEY>`               | Set Brave Search API key (DDG used if empty)     |
| `config_show`                        | Show all current config                          |
| `config_reset`                       | Clear NVS, revert to build-time defaults         |

**Debug & maintenance:**

| Command                        | Description                          |
|--------------------------------|--------------------------------------|
| `wifi_status`                  | Show connection status and IP        |
| `wifi_scan`                    | Scan nearby access points            |
| `memory_read`                  | Print MEMORY.md contents             |
| `memory_write <CONTENT>`       | Overwrite MEMORY.md                  |
| `session_list`                 | List all session files               |
| `session_clear <CHAT_ID>`      | Delete a session file                |
| `heap_info`                    | Show internal + PSRAM free bytes     |
| `skill_list`                   | List installed skills                |
| `heartbeat_trigger`            | Manually trigger heartbeat           |
| `cron_start`                   | Start cron scheduler                 |
| `restart`                      | Reboot the device                    |
| `help`                         | List all available commands          |

---

## Nanobot Reference Mapping

| Nanobot Module              | FemtoClaw Equivalent            | Notes                        |
|-----------------------------|--------------------------------|------------------------------|
| `agent/loop.py`             | `agent/agent_loop.c`           | ReAct loop with tool use     |
| `agent/context.py`          | `agent/context_builder.c`      | Loads SOUL.md + USER.md + memory + tool guidance |
| `agent/memory.py`           | `memory/memory_store.c`        | MEMORY.md + daily notes      |
| `session/manager.py`        | `memory/session_mgr.c`         | JSONL per chat, ring buffer  |
| `channels/telegram.py`      | `telegram/telegram_bot.c`      | Raw HTTP, no python-telegram-bot |
| `bus/events.py` + `queue.py`| `bus/message_bus.c`            | FreeRTOS queues vs asyncio   |
| `providers/litellm_provider.py` | `llm/llm_proxy.c`         | Anthropic + OpenAI + Ollama/LM Studio |
| `config/schema.py`          | `femto_config.h` + `femto_secrets.h` | Build-time secrets only  |
| `cli/commands.py`           | `cli/serial_cli.c`             | esp_console REPL             |
| `agent/tools/*`             | `tools/tool_registry.c` + `tool_web_search.c` | Brave Search + DuckDuckGo fallback |
| `agent/subagent.py`         | *(not yet implemented)*        | See TODO.md                  |
| `agent/skills.py`           | *(not yet implemented)*        | See TODO.md                  |
| `cron/service.py`           | *(not yet implemented)*        | See TODO.md                  |
| `heartbeat/service.py`      | *(not yet implemented)*        | See TODO.md                  |
