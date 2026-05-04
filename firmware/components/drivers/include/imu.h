#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int16_t accel_mg[3];
    int16_t gyro_mdps[3];
    int16_t mag_mgauss[3];
} imu_sample_t;

esp_err_t imu_init(void);
esp_err_t imu_read(imu_sample_t *sample);
