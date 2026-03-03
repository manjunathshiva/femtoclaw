#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Control GPIO pins (set output level or read input level).
 * Input JSON: { pin, action ("set"|"read"), level? (0|1, required for "set") }
 */
esp_err_t tool_gpio_control_execute(const char *input_json, char *output, size_t output_size);
