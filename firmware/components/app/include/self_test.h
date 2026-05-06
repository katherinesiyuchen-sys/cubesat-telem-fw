#pragma once

#include <stddef.h>
#include <stdint.h>

#include "diagnostic_protocol.h"
#include "esp_err.h"
#include "packet.h"

esp_err_t self_test_run(diagnostic_report_t *report, uint32_t boot_count);
void self_test_log_report(const diagnostic_report_t *report);
esp_err_t self_test_encode_report_packet(
    const diagnostic_report_t *report,
    uint8_t *encoded,
    size_t encoded_capacity,
    size_t *out_len,
    hope_packet_t *out_packet
);
esp_err_t self_test_emit_report_packet(const diagnostic_report_t *report);
