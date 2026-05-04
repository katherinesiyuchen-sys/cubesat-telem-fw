#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t temp_sensor_init(void);
esp_err_t temp_sensor_read_c_x10(int16_t *temperature_c_x10);
