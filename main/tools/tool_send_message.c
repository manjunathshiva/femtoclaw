#include "tools/tool_send_message.h"
#include "bus/message_bus.h"

#include <string.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tool_send_message";

esp_err_t tool_send_message_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "chat_id"));
    const char *message = cJSON_GetStringValue(cJSON_GetObjectItem(root, "message"));
    const char *channel = cJSON_GetStringValue(cJSON_GetObjectItem(root, "channel"));

    if (!chat_id || strlen(chat_id) == 0) {
        snprintf(output, output_size, "Error: missing or empty 'chat_id'");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (!message || strlen(message) == 0) {
        snprintf(output, output_size, "Error: missing or empty 'message'");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (!channel) {
        channel = FEMTO_CHAN_TELEGRAM;
    }

    femto_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.channel, channel, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
    msg.content = strdup(message);
    if (!msg.content) {
        snprintf(output, output_size, "Error: out of memory");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = message_bus_push_outbound(&msg);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        free(msg.content);
        snprintf(output, output_size, "Error: failed to queue message (%s)", esp_err_to_name(err));
        return err;
    }

    snprintf(output, output_size, "OK: Message queued for delivery to %s:%s", msg.channel, msg.chat_id);
    ESP_LOGI(TAG, "send_message: queued to %s:%s (%d bytes)", msg.channel, msg.chat_id, (int)strlen(message));
    return ESP_OK;
}
