#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

typedef struct {
    uart_port_t uart_port;
    gpio_num_t pin_tx;
    gpio_num_t pin_rx;
    uint32_t baudrate;
} gnss_config_t;

typedef struct {
    bool valid;
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint8_t fix_type;
    uint8_t satellites;
} gnss_fix_t;

esp_err_t gnss_init(const gnss_config_t *config);
esp_err_t gnss_read_fix(gnss_fix_t *fix, uint32_t timeout_ms);
esp_err_t gnss_parse_sentence(const char *sentence, gnss_fix_t *fix);
