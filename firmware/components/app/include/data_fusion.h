#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "gnss.h"
#include "telemetry_protocol.h"

typedef struct {
    gnss_fix_t gnss;
    int16_t temperature_c_x10;
} cubesat_sensor_frame_t;

esp_err_t data_fusion_build_telemetry_sample(const cubesat_sensor_frame_t *frame, telemetry_sample_t *sample);
