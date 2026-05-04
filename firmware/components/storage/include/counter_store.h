#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t session_id;
    uint32_t counter;
} counter_store_record_t;

esp_err_t counter_store_load_tx(counter_store_record_t *record);
esp_err_t counter_store_save_tx(uint32_t session_id, uint32_t counter);
esp_err_t counter_store_load_rx(counter_store_record_t *record);
esp_err_t counter_store_save_rx(uint32_t session_id, uint32_t counter);
