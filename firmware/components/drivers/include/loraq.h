#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t pin_mosi;
    gpio_num_t pin_miso;
    gpio_num_t pin_sclk;
    gpio_num_t pin_cs;
    gpio_num_t pin_reset;
    gpio_num_t pin_busy;
    gpio_num_t pin_dio1;
    uint32_t frequency_hz;
    uint8_t spreading_factor;
    uint32_t bandwidth_hz;
    uint8_t coding_rate;
    int8_t tx_power_dbm;
} lora_config_t;

esp_err_t lora_init(const lora_config_t *config);
esp_err_t lora_send(const uint8_t *payload, size_t len, uint32_t timeout_ms);
esp_err_t lora_receive(uint8_t *payload, size_t capacity, size_t *out_len, uint32_t timeout_ms);
void lora_log_debug_snapshot(const char *context);
void lora_log_debug_changes(const char *context);
