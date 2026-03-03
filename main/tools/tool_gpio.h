#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Control GPIO pins: set/read level, blink a pin, or run a chase sequence.
 * Actions: "set", "read", "blink", "sequence", "stop"
 */
esp_err_t tool_gpio_control_execute(const char *input_json, char *output, size_t output_size);
