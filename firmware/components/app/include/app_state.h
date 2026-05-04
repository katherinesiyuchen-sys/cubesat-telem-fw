#pragma once

#include <stdint.h>

#include "config_store.h"
#include "esp_err.h"

typedef struct {
    cubesat_runtime_config_t config;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t replay_rejects;
    uint32_t parse_errors;
    uint32_t last_rx_counter;
    uint32_t last_tx_counter;
} cubesat_app_state_t;

esp_err_t app_state_init(void);
cubesat_app_state_t app_state_snapshot(void);
void app_state_record_tx(uint32_t counter);
void app_state_record_rx(uint32_t counter);
void app_state_record_replay_reject(void);
void app_state_record_parse_error(void);
