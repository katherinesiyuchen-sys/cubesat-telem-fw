#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

typedef struct {
    uart_port_t port;
    gpio_num_t tx_pin;
    gpio_num_t rx_pin;
    uint32_t baudrate;
    size_t rx_buffer_size;
    size_t tx_buffer_size;
} hal_uart_config_t;

esp_err_t hal_uart_init(const hal_uart_config_t *config);
int hal_uart_read(uart_port_t port, uint8_t *buffer, size_t len, uint32_t timeout_ms);
esp_err_t hal_uart_write(uart_port_t port, const uint8_t *data, size_t len);
esp_err_t hal_uart_read_line(uart_port_t port, char *line, size_t line_len, uint32_t timeout_ms);
