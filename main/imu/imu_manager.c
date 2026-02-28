#include "imu/imu_manager.h"

#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu/I2C_Driver.h"
#include "imu/QMI8658.h"

static const char *TAG = "imu";

static imu_shake_cb_t s_shake_cb = NULL;
static int64_t s_last_shake_us = 0;

static void imu_task(void *arg)
{
    (void)arg;
    const float threshold_g = 1.6f;
    const int64_t min_interval_us = 800000;

    while (1) {
        QMI8658_Loop();
        float ax = Accel.x;
        float ay = Accel.y;
        float az = Accel.z;
        float mag = sqrtf(ax * ax + ay * ay + az * az);
        float delta = fabsf(mag - 1.0f);

        if (delta > threshold_g) {
            int64_t now = esp_timer_get_time();
            if (now - s_last_shake_us > min_interval_us) {
                s_last_shake_us = now;
                ESP_LOGI(TAG, "Shake detected (delta=%.2f)", delta);
                if (s_shake_cb) {
                    s_shake_cb();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void imu_manager_init(void)
{
    I2C_Init();
    QMI8658_Init();
    xTaskCreatePinnedToCore(imu_task, "imu_task", 4096, NULL, 4, NULL, 0);
}

void imu_manager_set_shake_callback(imu_shake_cb_t cb)
{
    s_shake_cb = cb;
}
