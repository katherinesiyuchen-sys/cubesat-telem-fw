#pragma once

#include <stdint.h>

#include "esp_err.h"

#define TMP117_I2C_ADDR 0x48

esp_err_t temp_sensor_init(void);
esp_err_t temp_sensor_read_c_x10(int16_t *temperature_c_x10);
