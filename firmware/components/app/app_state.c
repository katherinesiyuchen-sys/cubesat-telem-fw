#include "app_state.h"

#include <string.h>

static cubesat_app_state_t s_state;

esp_err_t app_state_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    esp_err_t err = config_store_load(&s_state.config);
    if (err != ESP_OK) {
        config_store_defaults(&s_state.config);
    }
    return ESP_OK;
}

cubesat_app_state_t app_state_snapshot(void) {
    return s_state;
}

void app_state_record_tx(uint32_t counter) {
    s_state.tx_packets++;
    s_state.last_tx_counter = counter;
}

void app_state_record_rx(uint32_t counter) {
    s_state.rx_packets++;
    s_state.last_rx_counter = counter;
}

void app_state_record_replay_reject(void) {
    s_state.replay_rejects++;
}

void app_state_record_parse_error(void) {
    s_state.parse_errors++;
}
