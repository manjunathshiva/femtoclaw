#include "ota_manager.h"

#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"

static const char *TAG = "ota";

esp_err_t ota_update_from_url(const char *url)
{
    ESP_LOGI(TAG, "Starting OTA from: %s", url);

    bool is_https = (strncmp(url, "https://", 8) == 0);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 120000,
        .buffer_size = 4096,
    };

    if (is_https) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    } else {
        ESP_LOGW(TAG, "Using plain HTTP (no TLS) — only use on trusted local network");
        config.transport_type = HTTP_TRANSPORT_OVER_TCP;
    }

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful, restarting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }

    return ret;
}
