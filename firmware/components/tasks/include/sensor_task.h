#pragma once

#include <stdint.h>

#include "esp_err.h"

void sensor_task_start(void);
esp_err_t sensor_task_get_latest_temperature_c_x10(int16_t *temperature_c_x10);
