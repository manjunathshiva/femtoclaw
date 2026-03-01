#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Send a message immediately to a channel (Telegram, WebSocket).
 * Input JSON: { chat_id, message, channel? }
 */
esp_err_t tool_send_message_execute(const char *input_json, char *output, size_t output_size);
