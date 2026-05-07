#pragma once

#include "esp_err.h"
#include "gnss.h"
#include "packet.h"

typedef struct {
    int32_t latitude_e7;
    int32_t longitude_e7;
    int16_t temperature_c_x10;
    int32_t altitude_m_x10;
    uint16_t hdop_x100;
    uint16_t speed_mps_x100;
    uint16_t course_deg_x100;
    uint32_t fix_age_ms;
    uint32_t utc_time_ms;
    uint32_t utc_date_ddmmyy;
    uint8_t fix_type;
    uint8_t satellites;
    uint8_t gnss_flags;
} telemetry_sample_t;

#define TELEMETRY_PAYLOAD_V1_LEN 12
#define TELEMETRY_PAYLOAD_LEN    36

esp_err_t telemetry_protocol_build(const telemetry_sample_t *sample, hope_packet_t *pkt);
esp_err_t telemetry_protocol_build_from_gnss(const gnss_fix_t *fix, hope_packet_t *pkt);
esp_err_t telemetry_protocol_build_fake(hope_packet_t *pkt);
