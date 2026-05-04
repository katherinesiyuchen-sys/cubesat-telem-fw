#include "counter_store.h"

#include "nvs.h"
#include "nvs_store.h"

#define COUNTER_NS "cubesat_ctr"
#define TX_COUNTER_KEY "tx"
#define RX_COUNTER_KEY "rx"

static esp_err_t load_record(const char *key, counter_store_record_t *record) {
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = sizeof(*record);
    esp_err_t err = nvs_store_get_blob(COUNTER_NS, key, record, &len);
    if (err != ESP_OK) {
        return err;
    }
    return len == sizeof(*record) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t save_record(const char *key, uint32_t session_id, uint32_t counter) {
    counter_store_record_t record = {
        .session_id = session_id,
        .counter = counter,
    };
    return nvs_store_set_blob(COUNTER_NS, key, &record, sizeof(record));
}

esp_err_t counter_store_load_tx(counter_store_record_t *record) {
    return load_record(TX_COUNTER_KEY, record);
}

esp_err_t counter_store_save_tx(uint32_t session_id, uint32_t counter) {
    return save_record(TX_COUNTER_KEY, session_id, counter);
}

esp_err_t counter_store_load_rx(counter_store_record_t *record) {
    return load_record(RX_COUNTER_KEY, record);
}

esp_err_t counter_store_save_rx(uint32_t session_id, uint32_t counter) {
    return save_record(RX_COUNTER_KEY, session_id, counter);
}
