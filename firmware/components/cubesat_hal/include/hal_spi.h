#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
    spi_host_device_t host;
    gpio_num_t mosi_pin;
    gpio_num_t miso_pin;
    gpio_num_t sclk_pin;
    int max_transfer_sz;
} hal_spi_bus_config_t;

esp_err_t hal_spi_bus_init(const hal_spi_bus_config_t *config);
esp_err_t hal_spi_add_device(spi_host_device_t host, gpio_num_t cs_pin, int clock_hz, spi_device_handle_t *out);
esp_err_t hal_spi_transfer(spi_device_handle_t device, const uint8_t *tx, uint8_t *rx, size_t len);
