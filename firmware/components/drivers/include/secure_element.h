#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t secure_element_init(void);
esp_err_t secure_element_random(uint8_t *out, size_t len);
