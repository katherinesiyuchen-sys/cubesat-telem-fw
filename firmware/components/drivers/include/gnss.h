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
    int32_t altitude_m_x10;
    uint16_t hdop_x100;
    uint16_t speed_mps_x100;
    uint16_t course_deg_x100;
    uint32_t fix_age_ms;
    uint32_t utc_time_ms;
    uint32_t utc_date_ddmmyy;
    uint8_t fix_type;
    uint8_t satellites;
    uint8_t source_flags;
} gnss_fix_t;

#define GNSS_FIX_FLAG_RMC       0x01
#define GNSS_FIX_FLAG_GGA       0x02
#define GNSS_FIX_FLAG_CHECKSUM  0x04
#define GNSS_FIX_FLAG_STALE     0x80

esp_err_t gnss_init(const gnss_config_t *config);
esp_err_t gnss_read_fix(gnss_fix_t *fix, uint32_t timeout_ms);
esp_err_t gnss_parse_sentence(const char *sentence, gnss_fix_t *fix);
