#include "config_store.h"

#include <string.h>

#include "board_config.h"
#include "nvs.h"
#include "nvs_store.h"

#define CONFIG_NS "cubesat_cfg"
#define CONFIG_KEY "runtime"

void config_store_defaults(cubesat_runtime_config_t *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->node_id = CUBESAT_NODE_ID;
    config->ground_id = CUBESAT_GROUND_ID;
    config->session_id = CUBESAT_DEMO_SESSION_ID;
    config->lora_frequency_hz = CUBESAT_LORA_FREQUENCY_HZ;
    config->lora_spreading_factor = CUBESAT_LORA_SPREADING_FACTOR;
    config->lora_bandwidth_hz = CUBESAT_LORA_BANDWIDTH_HZ;
    config->lora_coding_rate = CUBESAT_LORA_CODING_RATE;
    config->lora_tx_power_dbm = CUBESAT_LORA_TX_POWER_DBM;
    config->gnss_baudrate = CUBESAT_GNSS_BAUDRATE;
}

esp_err_t config_store_load(cubesat_runtime_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_store_defaults(config);

    size_t len = sizeof(*config);
    esp_err_t err = nvs_store_get_blob(CONFIG_NS, CONFIG_KEY, config, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (len != sizeof(*config)) {
        config_store_defaults(config);
        return config_store_save(config);
    }

    return ESP_OK;
}

esp_err_t config_store_save(const cubesat_runtime_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return nvs_store_set_blob(CONFIG_NS, CONFIG_KEY, config, sizeof(*config));
}

esp_err_t config_store_increment_boot_count(uint32_t *boot_count) {
    cubesat_runtime_config_t config;
    esp_err_t err = config_store_load(&config);
    if (err != ESP_OK) {
        return err;
    }

    config.boot_count++;
    err = config_store_save(&config);
    if (err == ESP_OK && boot_count != NULL) {
        *boot_count = config.boot_count;
    }
    return err;
}
