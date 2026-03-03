#include "tools/tool_gpio.h"

#include <string.h>
#include "esp_log.h"
#include "cJSON.h"
#include "driver/gpio.h"

static const char *TAG = "tool_gpio";

#if CONFIG_IDF_TARGET_ESP32S3
static const int s_safe_pins[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    21, 38, 39, 40, 41, 42, 48
};
#else
/* ESP32-WROOM-32 */
static const int s_safe_pins[] = {
    2, 4, 5, 12, 13, 14, 15, 18, 19, 22, 23, 25, 26, 27, 32, 33
};
#endif

#define SAFE_PIN_COUNT (sizeof(s_safe_pins) / sizeof(s_safe_pins[0]))

static bool is_pin_allowed(int pin)
{
    for (size_t i = 0; i < SAFE_PIN_COUNT; i++) {
        if (s_safe_pins[i] == pin) return true;
    }
    return false;
}

esp_err_t tool_gpio_control_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *pin_item = cJSON_GetObjectItem(root, "pin");
    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));

    if (!pin_item || !cJSON_IsNumber(pin_item)) {
        snprintf(output, output_size, "Error: missing or invalid 'pin' (integer required)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (!action || strlen(action) == 0) {
        snprintf(output, output_size, "Error: missing or empty 'action' (must be 'set' or 'read')");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int pin = pin_item->valueint;

    if (!is_pin_allowed(pin)) {
        snprintf(output, output_size, "Error: GPIO %d is not in the safe pin allowlist", pin);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_num_t gpio = (gpio_num_t)pin;

    if (strcmp(action, "set") == 0) {
        cJSON *level_item = cJSON_GetObjectItem(root, "level");
        if (!level_item || !cJSON_IsNumber(level_item)) {
            snprintf(output, output_size, "Error: 'level' (0 or 1) is required for action 'set'");
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
        int level = level_item->valueint;
        if (level != 0 && level != 1) {
            snprintf(output, output_size, "Error: 'level' must be 0 or 1, got %d", level);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }

        gpio_reset_pin(gpio);
        gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(gpio, level);

        snprintf(output, output_size, "OK: GPIO %d set to %s", pin, level ? "HIGH" : "LOW");
        ESP_LOGI(TAG, "GPIO %d set to %s", pin, level ? "HIGH" : "LOW");

    } else if (strcmp(action, "read") == 0) {
        gpio_reset_pin(gpio);
        gpio_set_direction(gpio, GPIO_MODE_INPUT);
        int level = gpio_get_level(gpio);

        snprintf(output, output_size, "OK: GPIO %d level is %s (%d)", pin, level ? "HIGH" : "LOW", level);
        ESP_LOGI(TAG, "GPIO %d read: %s (%d)", pin, level ? "HIGH" : "LOW", level);

    } else {
        snprintf(output, output_size, "Error: unknown action '%s' (must be 'set' or 'read')", action);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON_Delete(root);
    return ESP_OK;
}
