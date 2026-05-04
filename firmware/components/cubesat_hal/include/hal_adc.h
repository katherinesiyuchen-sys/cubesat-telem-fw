#pragma once

#include "esp_err.h"

esp_err_t hal_adc_read_millivolts(int channel, int *millivolts);
