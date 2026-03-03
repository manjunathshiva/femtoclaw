#include "tools/tool_gpio.h"

#include <string.h>
#include "esp_log.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_timer.h"

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
#define MAX_SEQ_PINS   16

static bool is_pin_allowed(int pin)
{
    for (size_t i = 0; i < SAFE_PIN_COUNT; i++) {
        if (s_safe_pins[i] == pin) return true;
    }
    return false;
}

/* --- Animation state --- */

typedef enum { ANIM_NONE, ANIM_BLINK, ANIM_SEQUENCE } anim_mode_t;

static struct {
    esp_timer_handle_t timer;
    anim_mode_t mode;
    int pins[MAX_SEQ_PINS];
    int pin_count;
    int current_idx;
    int level;
    int cycles_done;
    int cycles_total;   /* 0 = infinite */
} s_anim;

static void anim_stop_internal(void)
{
    if (s_anim.timer) {
        esp_timer_stop(s_anim.timer);
    }
    for (int i = 0; i < s_anim.pin_count; i++) {
        gpio_set_level((gpio_num_t)s_anim.pins[i], 0);
    }
    s_anim.mode = ANIM_NONE;
    s_anim.pin_count = 0;
    ESP_LOGI(TAG, "Animation stopped");
}

static void anim_timer_cb(void *arg)
{
    if (s_anim.mode == ANIM_BLINK) {
        s_anim.level = !s_anim.level;
        gpio_set_level((gpio_num_t)s_anim.pins[0], s_anim.level);
        if (!s_anim.level) {
            s_anim.cycles_done++;
            if (s_anim.cycles_total > 0 && s_anim.cycles_done >= s_anim.cycles_total) {
                anim_stop_internal();
            }
        }
    } else if (s_anim.mode == ANIM_SEQUENCE) {
        gpio_set_level((gpio_num_t)s_anim.pins[s_anim.current_idx], 0);
        s_anim.current_idx++;
        if (s_anim.current_idx >= s_anim.pin_count) {
            s_anim.current_idx = 0;
            s_anim.cycles_done++;
            if (s_anim.cycles_total > 0 && s_anim.cycles_done >= s_anim.cycles_total) {
                anim_stop_internal();
                return;
            }
        }
        gpio_set_level((gpio_num_t)s_anim.pins[s_anim.current_idx], 1);
    }
}

static void ensure_timer_created(void)
{
    if (s_anim.timer) return;
    esp_timer_create_args_t args = {
        .callback = anim_timer_cb,
        .name = "gpio_anim",
    };
    esp_timer_create(&args, &s_anim.timer);
}

static esp_err_t handle_set(cJSON *root, int pin, char *output, size_t output_size)
{
    cJSON *level_item = cJSON_GetObjectItem(root, "level");
    if (!level_item || !cJSON_IsNumber(level_item)) {
        snprintf(output, output_size, "Error: 'level' (0 or 1) is required for action 'set'");
        return ESP_ERR_INVALID_ARG;
    }
    int level = level_item->valueint;
    if (level != 0 && level != 1) {
        snprintf(output, output_size, "Error: 'level' must be 0 or 1, got %d", level);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, level);

    snprintf(output, output_size, "OK: GPIO %d set to %s", pin, level ? "HIGH" : "LOW");
    ESP_LOGI(TAG, "GPIO %d set to %s", pin, level ? "HIGH" : "LOW");
    return ESP_OK;
}

static esp_err_t handle_read(int pin, char *output, size_t output_size)
{
    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
    int level = gpio_get_level((gpio_num_t)pin);

    snprintf(output, output_size, "OK: GPIO %d level is %s (%d)", pin, level ? "HIGH" : "LOW", level);
    ESP_LOGI(TAG, "GPIO %d read: %s (%d)", pin, level ? "HIGH" : "LOW", level);
    return ESP_OK;
}

static esp_err_t handle_blink(cJSON *root, int pin, char *output, size_t output_size)
{
    cJSON *interval_item = cJSON_GetObjectItem(root, "interval_ms");
    cJSON *count_item = cJSON_GetObjectItem(root, "count");

    int interval_ms = (interval_item && cJSON_IsNumber(interval_item)) ? interval_item->valueint : 500;
    int count = (count_item && cJSON_IsNumber(count_item)) ? count_item->valueint : 0;

    if (interval_ms < 50 || interval_ms > 10000) {
        snprintf(output, output_size, "Error: interval_ms must be 50-10000, got %d", interval_ms);
        return ESP_ERR_INVALID_ARG;
    }

    anim_stop_internal();
    ensure_timer_created();

    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, 1);

    s_anim.mode = ANIM_BLINK;
    s_anim.pins[0] = pin;
    s_anim.pin_count = 1;
    s_anim.level = 1;
    s_anim.current_idx = 0;
    s_anim.cycles_done = 0;
    s_anim.cycles_total = count;

    esp_timer_start_periodic(s_anim.timer, (uint64_t)interval_ms * 1000);

    snprintf(output, output_size, "OK: GPIO %d blinking every %d ms%s",
             pin, interval_ms, count > 0 ? "" : " (infinite, use 'stop' to end)");
    ESP_LOGI(TAG, "Blink started: pin %d, interval %d ms, count %d", pin, interval_ms, count);
    return ESP_OK;
}

static esp_err_t handle_sequence(cJSON *root, char *output, size_t output_size)
{
    cJSON *pins_arr = cJSON_GetObjectItem(root, "pins");
    cJSON *interval_item = cJSON_GetObjectItem(root, "interval_ms");
    cJSON *count_item = cJSON_GetObjectItem(root, "count");

    if (!pins_arr || !cJSON_IsArray(pins_arr)) {
        snprintf(output, output_size, "Error: 'pins' array is required for action 'sequence'");
        return ESP_ERR_INVALID_ARG;
    }

    int n = cJSON_GetArraySize(pins_arr);
    if (n < 2 || n > MAX_SEQ_PINS) {
        snprintf(output, output_size, "Error: 'pins' must have 2-%d elements, got %d", MAX_SEQ_PINS, n);
        return ESP_ERR_INVALID_ARG;
    }

    int pins[MAX_SEQ_PINS];
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(pins_arr, i);
        if (!item || !cJSON_IsNumber(item)) {
            snprintf(output, output_size, "Error: pins[%d] is not a number", i);
            return ESP_ERR_INVALID_ARG;
        }
        pins[i] = item->valueint;
        if (!is_pin_allowed(pins[i])) {
            snprintf(output, output_size, "Error: GPIO %d is not in the safe pin allowlist", pins[i]);
            return ESP_ERR_INVALID_ARG;
        }
    }

    int interval_ms = (interval_item && cJSON_IsNumber(interval_item)) ? interval_item->valueint : 200;
    int count = (count_item && cJSON_IsNumber(count_item)) ? count_item->valueint : 0;

    if (interval_ms < 50 || interval_ms > 10000) {
        snprintf(output, output_size, "Error: interval_ms must be 50-10000, got %d", interval_ms);
        return ESP_ERR_INVALID_ARG;
    }

    anim_stop_internal();
    ensure_timer_created();

    for (int i = 0; i < n; i++) {
        gpio_reset_pin((gpio_num_t)pins[i]);
        gpio_set_direction((gpio_num_t)pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)pins[i], 0);
        s_anim.pins[i] = pins[i];
    }

    s_anim.mode = ANIM_SEQUENCE;
    s_anim.pin_count = n;
    s_anim.current_idx = 0;
    s_anim.cycles_done = 0;
    s_anim.cycles_total = count;

    gpio_set_level((gpio_num_t)s_anim.pins[0], 1);

    esp_timer_start_periodic(s_anim.timer, (uint64_t)interval_ms * 1000);

    snprintf(output, output_size, "OK: Sequence running on %d pins every %d ms%s",
             n, interval_ms, count > 0 ? "" : " (infinite, use 'stop' to end)");
    ESP_LOGI(TAG, "Sequence started: %d pins, interval %d ms, count %d", n, interval_ms, count);
    return ESP_OK;
}

esp_err_t tool_gpio_control_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
    if (!action || strlen(action) == 0) {
        snprintf(output, output_size, "Error: missing or empty 'action'");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    /* stop needs no pin */
    if (strcmp(action, "stop") == 0) {
        if (s_anim.mode == ANIM_NONE) {
            snprintf(output, output_size, "OK: No animation running");
        } else {
            anim_stop_internal();
            snprintf(output, output_size, "OK: Animation stopped, all pins set LOW");
        }
        cJSON_Delete(root);
        return ESP_OK;
    }

    /* sequence uses 'pins' array, not 'pin' */
    if (strcmp(action, "sequence") == 0) {
        esp_err_t ret = handle_sequence(root, output, output_size);
        cJSON_Delete(root);
        return ret;
    }

    /* remaining actions need a single 'pin' */
    cJSON *pin_item = cJSON_GetObjectItem(root, "pin");
    if (!pin_item || !cJSON_IsNumber(pin_item)) {
        snprintf(output, output_size, "Error: missing or invalid 'pin' (integer required)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int pin = pin_item->valueint;
    if (!is_pin_allowed(pin)) {
        snprintf(output, output_size, "Error: GPIO %d is not in the safe pin allowlist", pin);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    if (strcmp(action, "set") == 0) {
        ret = handle_set(root, pin, output, output_size);
    } else if (strcmp(action, "read") == 0) {
        ret = handle_read(pin, output, output_size);
    } else if (strcmp(action, "blink") == 0) {
        ret = handle_blink(root, pin, output, output_size);
    } else {
        snprintf(output, output_size, "Error: unknown action '%s' (must be set/read/blink/sequence/stop)", action);
        ret = ESP_ERR_INVALID_ARG;
    }

    cJSON_Delete(root);
    return ret;
}
