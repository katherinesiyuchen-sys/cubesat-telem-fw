#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "packet.h"

#define DIAGNOSTIC_PAYLOAD_VERSION 1
#define DIAGNOSTIC_PIN_COUNT 12
#define DIAGNOSTIC_PAYLOAD_LEN 39

typedef enum {
    DIAGNOSTIC_STATUS_PASS = 0,
    DIAGNOSTIC_STATUS_WARN = 1,
    DIAGNOSTIC_STATUS_FAIL = 2,
} diagnostic_status_t;

typedef enum {
    DIAGNOSTIC_CHECK_PIN_MAP    = (1U << 0),
    DIAGNOSTIC_CHECK_I2C_BUS    = (1U << 1),
    DIAGNOSTIC_CHECK_GNSS_UART  = (1U << 2),
    DIAGNOSTIC_CHECK_LORA_RADIO = (1U << 3),
    DIAGNOSTIC_CHECK_SECURE_RNG = (1U << 4),
    DIAGNOSTIC_CHECK_NVS_CONFIG = (1U << 5),
} diagnostic_check_mask_t;

typedef struct {
    uint8_t version;
    uint8_t overall_status;
    uint16_t passed_mask;
    uint16_t warning_mask;
    uint16_t failed_mask;
    uint32_t uptime_s;
    uint32_t boot_count;
    int16_t lora_status;
    int16_t gnss_status;
    int16_t i2c_status;
    int16_t rng_status;
    int16_t nvs_status;
    uint8_t i2c_devices_seen;
    uint8_t pins[DIAGNOSTIC_PIN_COUNT];
} diagnostic_report_t;

esp_err_t diagnostic_protocol_build_payload(
    const diagnostic_report_t *report,
    uint8_t *payload,
    size_t payload_len
);

esp_err_t diagnostic_protocol_parse_payload(
    const uint8_t *payload,
    size_t payload_len,
    diagnostic_report_t *report
);

esp_err_t diagnostic_protocol_build_packet(
    const diagnostic_report_t *report,
    hope_packet_t *pkt
);
