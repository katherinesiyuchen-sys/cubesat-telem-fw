#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t node_id;
    uint16_t ground_id;
    uint32_t session_id;
    uint32_t lora_frequency_hz;
    uint8_t lora_spreading_factor;
    uint32_t lora_bandwidth_hz;
    uint8_t lora_coding_rate;
    int8_t lora_tx_power_dbm;
    uint32_t gnss_baudrate;
    uint8_t transport_mode;
    uint8_t cadence_mode;
    uint8_t reserved[2];
    uint32_t boot_count;
} cubesat_runtime_config_t;

void config_store_defaults(cubesat_runtime_config_t *config);
esp_err_t config_store_load(cubesat_runtime_config_t *config);
esp_err_t config_store_save(const cubesat_runtime_config_t *config);
esp_err_t config_store_increment_boot_count(uint32_t *boot_count);
