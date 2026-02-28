#pragma once

#include <stdbool.h>

typedef void (*imu_shake_cb_t)(void);

void imu_manager_init(void);
void imu_manager_set_shake_callback(imu_shake_cb_t cb);
