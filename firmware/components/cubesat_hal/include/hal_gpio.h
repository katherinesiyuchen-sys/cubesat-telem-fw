#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

esp_err_t hal_gpio_config_output(gpio_num_t pin, bool initial_high);
esp_err_t hal_gpio_config_input(gpio_num_t pin, bool pullup, bool pulldown);
esp_err_t hal_gpio_write(gpio_num_t pin, bool high);
esp_err_t hal_gpio_read(gpio_num_t pin, bool *high);
esp_err_t hal_gpio_toggle(gpio_num_t pin);
