#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

typedef struct {
    i2c_port_t port;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    uint32_t clock_hz;
} hal_i2c_config_t;

esp_err_t hal_i2c_master_init(const hal_i2c_config_t *config);
esp_err_t hal_i2c_write(uint8_t address, const uint8_t *data, size_t len, uint32_t timeout_ms);
esp_err_t hal_i2c_write_read(uint8_t address, const uint8_t *write_data, size_t write_len, uint8_t *read_data, size_t read_len, uint32_t timeout_ms);
