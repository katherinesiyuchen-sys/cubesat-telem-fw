#include "hal_i2c.h"

#include "freertos/FreeRTOS.h"

static i2c_port_t s_port = I2C_NUM_0;

esp_err_t hal_i2c_master_init(const hal_i2c_config_t *config) {
    if (config == NULL || config->clock_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_port = config->port;
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = config->clock_hz,
    };

    esp_err_t err = i2c_param_config(config->port, &i2c_config);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(config->port, I2C_MODE_MASTER, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return err;
}

esp_err_t hal_i2c_write(uint8_t address, const uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_to_device(s_port, address, data, len, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t hal_i2c_write_read(uint8_t address, const uint8_t *write_data, size_t write_len, uint8_t *read_data, size_t read_len, uint32_t timeout_ms) {
    if (write_data == NULL || write_len == 0 || read_data == NULL || read_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_read_device(s_port, address, write_data, write_len, read_data, read_len, pdMS_TO_TICKS(timeout_ms));
}
