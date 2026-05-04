#pragma once

#include "esp_err.h"
#include "gnss.h"
#include "packet.h"

typedef struct {
    int32_t latitude_e7;
    int32_t longitude_e7;
    int16_t temperature_c_x10;
    uint8_t fix_type;
    uint8_t satellites;
} telemetry_sample_t;

#define TELEMETRY_PAYLOAD_LEN 12

esp_err_t telemetry_protocol_build(const telemetry_sample_t *sample, hope_packet_t *pkt);
esp_err_t telemetry_protocol_build_from_gnss(const gnss_fix_t *fix, hope_packet_t *pkt);
esp_err_t telemetry_protocol_build_fake(hope_packet_t *pkt);
