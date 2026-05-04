#pragma once

#include <stdint.h>

#include "diagnostic_protocol.h"
#include "esp_err.h"

esp_err_t self_test_run(diagnostic_report_t *report, uint32_t boot_count);
void self_test_log_report(const diagnostic_report_t *report);
esp_err_t self_test_emit_report_packet(const diagnostic_report_t *report);
