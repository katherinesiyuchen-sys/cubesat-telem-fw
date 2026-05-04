#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t bus_mv;
    uint16_t current_ma;
    uint8_t battery_percent;
} power_monitor_sample_t;

esp_err_t power_monitor_init(void);
esp_err_t power_monitor_read(power_monitor_sample_t *sample);
