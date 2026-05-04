#include "hal_i2c.h"

#include <limits.h>
#include <stdbool.h>

#define HAL_I2C_MAX_DEVICES 16
#define HAL_I2C_DEFAULT_GLITCH_IGNORE 7

typedef struct {
    uint8_t address;
    i2c_master_dev_handle_t handle;
} cached_i2c_device_t;

static i2c_master_bus_handle_t s_bus = NULL;
static cached_i2c_device_t s_devices[HAL_I2C_MAX_DEVICES];
static size_t s_device_count = 0;
static hal_i2c_config_t s_config;

static int timeout_to_ms(uint32_t timeout_ms) {
    return timeout_ms > (uint32_t)INT_MAX ? INT_MAX : (int)timeout_ms;
}

static bool config_matches_existing(const hal_i2c_config_t *config) {
    return s_config.port == config->port &&
        s_config.sda_pin == config->sda_pin &&
        s_config.scl_pin == config->scl_pin &&
        s_config.clock_hz == config->clock_hz;
}

static esp_err_t get_device(uint8_t address, i2c_master_dev_handle_t *out) {
    if (out == NULL || address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i].address == address) {
            *out = s_devices[i].handle;
            return ESP_OK;
        }
    }

    if (s_device_count >= HAL_I2C_MAX_DEVICES) {
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = s_config.clock_hz,
        .scl_wait_us = 0,
    };

    i2c_master_dev_handle_t handle = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, &handle);
    if (err != ESP_OK) {
        return err;
    }

    s_devices[s_device_count].address = address;
    s_devices[s_device_count].handle = handle;
    s_device_count++;
    *out = handle;
    return ESP_OK;
}

esp_err_t hal_i2c_master_init(const hal_i2c_config_t *config) {
    if (config == NULL || config->clock_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_bus != NULL) {
        return config_matches_existing(config) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = config->port,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = HAL_I2C_DEFAULT_GLITCH_IGNORE,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        return err;
    }

    s_config = *config;
    s_device_count = 0;
    return ESP_OK;
}

esp_err_t hal_i2c_probe(uint8_t address, uint32_t timeout_ms) {
    if (address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_probe(s_bus, address, timeout_to_ms(timeout_ms));
}

esp_err_t hal_i2c_write(uint8_t address, const uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t device = NULL;
    esp_err_t err = get_device(address, &device);
    if (err != ESP_OK) {
        return err;
    }

    return i2c_master_transmit(device, data, len, timeout_to_ms(timeout_ms));
}

esp_err_t hal_i2c_write_read(uint8_t address, const uint8_t *write_data, size_t write_len, uint8_t *read_data, size_t read_len, uint32_t timeout_ms) {
    if (write_data == NULL || write_len == 0 || read_data == NULL || read_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t device = NULL;
    esp_err_t err = get_device(address, &device);
    if (err != ESP_OK) {
        return err;
    }

    return i2c_master_transmit_receive(device, write_data, write_len, read_data, read_len, timeout_to_ms(timeout_ms));
}

esp_err_t hal_i2c_bus_reset(void) {
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_bus_reset(s_bus);
}
