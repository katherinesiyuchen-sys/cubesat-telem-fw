#include "hal_uart.h"

#include "freertos/FreeRTOS.h"

esp_err_t hal_uart_init(const hal_uart_config_t *config) {
    if (config == NULL || config->rx_buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uart_config_t uart_config = {
        .baud_rate = (int)config->baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(
        config->port,
        (int)config->rx_buffer_size,
        (int)config->tx_buffer_size,
        0,
        NULL,
        0
    );
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = uart_param_config(config->port, &uart_config);
    if (err != ESP_OK) {
        return err;
    }

    return uart_set_pin(config->port, config->tx_pin, config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

int hal_uart_read(uart_port_t port, uint8_t *buffer, size_t len, uint32_t timeout_ms) {
    if (buffer == NULL || len == 0) {
        return -1;
    }

    return uart_read_bytes(port, buffer, (uint32_t)len, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t hal_uart_write(uart_port_t port, const uint8_t *data, size_t len) {
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = uart_write_bytes(port, data, len);
    return written == (int)len ? ESP_OK : ESP_FAIL;
}

esp_err_t hal_uart_read_line(uart_port_t port, char *line, size_t line_len, uint32_t timeout_ms) {
    if (line == NULL || line_len < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t used = 0;
    while (used < line_len - 1) {
        uint8_t ch = 0;
        int read = uart_read_bytes(port, &ch, 1, pdMS_TO_TICKS(timeout_ms));
        if (read <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            line[used] = '\0';
            return used == 0 ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
        }
        line[used++] = (char)ch;
    }

    line[used] = '\0';
    return ESP_ERR_INVALID_SIZE;
}
