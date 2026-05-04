#include "hal_spi.h"

esp_err_t hal_spi_bus_init(const hal_spi_bus_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = config->miso_pin,
        .sclk_io_num = config->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = config->max_transfer_sz,
    };

    esp_err_t err = spi_bus_initialize(config->host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return err;
}

esp_err_t hal_spi_add_device(spi_host_device_t host, gpio_num_t cs_pin, int clock_hz, spi_device_handle_t *out) {
    if (out == NULL || clock_hz <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = clock_hz,
        .mode = 0,
        .spics_io_num = cs_pin,
        .queue_size = 1,
    };

    return spi_bus_add_device(host, &dev_cfg, out);
}

esp_err_t hal_spi_transfer(spi_device_handle_t device, const uint8_t *tx, uint8_t *rx, size_t len) {
    if (device == NULL || len == 0 || (tx == NULL && rx == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t transaction = {
        .length = 8 * len,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    return spi_device_transmit(device, &transaction);
}
