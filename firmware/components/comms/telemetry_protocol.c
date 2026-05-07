#include "telemetry_protocol.h"

#include <string.h>

static void write_i16_be(uint8_t *buf, int16_t value) {
    uint16_t raw = (uint16_t)value;
    buf[0] = (uint8_t)((raw >> 8) & 0xFF);
    buf[1] = (uint8_t)(raw & 0xFF);
}

static void write_u16_be(uint8_t *buf, uint16_t value) {
    buf[0] = (uint8_t)((value >> 8) & 0xFF);
    buf[1] = (uint8_t)(value & 0xFF);
}

static void write_i32_be(uint8_t *buf, int32_t value) {
    uint32_t raw = (uint32_t)value;
    buf[0] = (uint8_t)((raw >> 24) & 0xFF);
    buf[1] = (uint8_t)((raw >> 16) & 0xFF);
    buf[2] = (uint8_t)((raw >> 8) & 0xFF);
    buf[3] = (uint8_t)(raw & 0xFF);
}

static void write_u32_be(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)((value >> 24) & 0xFF);
    buf[1] = (uint8_t)((value >> 16) & 0xFF);
    buf[2] = (uint8_t)((value >> 8) & 0xFF);
    buf[3] = (uint8_t)(value & 0xFF);
}

esp_err_t telemetry_protocol_build(const telemetry_sample_t *sample, hope_packet_t *pkt) {
    if (sample == NULL || pkt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(pkt, 0, sizeof(*pkt));

    pkt->version = HOPE_PACKET_VERSION;
    pkt->type = HOPE_PACKET_TYPE_TELEMETRY;

    pkt->src_id = 1;
    pkt->dst_id = 2;

    pkt->timestamp = 0;

    pkt->payload_len = TELEMETRY_PAYLOAD_LEN;
    write_i32_be(&pkt->payload[0], sample->latitude_e7);
    write_i32_be(&pkt->payload[4], sample->longitude_e7);
    write_i16_be(&pkt->payload[8], sample->temperature_c_x10);
    pkt->payload[10] = sample->fix_type;
    pkt->payload[11] = sample->satellites;
    write_i32_be(&pkt->payload[12], sample->altitude_m_x10);
    write_u16_be(&pkt->payload[16], sample->hdop_x100);
    write_u16_be(&pkt->payload[18], sample->speed_mps_x100);
    write_u16_be(&pkt->payload[20], sample->course_deg_x100);
    pkt->payload[22] = sample->gnss_flags;
    pkt->payload[23] = 0;
    write_u32_be(&pkt->payload[24], sample->fix_age_ms);
    write_u32_be(&pkt->payload[28], sample->utc_time_ms);
    write_u32_be(&pkt->payload[32], sample->utc_date_ddmmyy);

    return ESP_OK;
}

esp_err_t telemetry_protocol_build_from_gnss(const gnss_fix_t *fix, hope_packet_t *pkt) {
    if (fix == NULL || !fix->valid) {
        return ESP_ERR_INVALID_STATE;
    }

    telemetry_sample_t sample = {
        .latitude_e7 = fix->latitude_e7,
        .longitude_e7 = fix->longitude_e7,
        .temperature_c_x10 = 0,
        .altitude_m_x10 = fix->altitude_m_x10,
        .hdop_x100 = fix->hdop_x100,
        .speed_mps_x100 = fix->speed_mps_x100,
        .course_deg_x100 = fix->course_deg_x100,
        .fix_age_ms = fix->fix_age_ms,
        .utc_time_ms = fix->utc_time_ms,
        .utc_date_ddmmyy = fix->utc_date_ddmmyy,
        .fix_type = fix->fix_type,
        .satellites = fix->satellites,
        .gnss_flags = fix->source_flags,
    };

    return telemetry_protocol_build(&sample, pkt);
}

esp_err_t telemetry_protocol_build_fake(hope_packet_t *pkt) {
    telemetry_sample_t sample = {
        .latitude_e7 = 378715000,
        .longitude_e7 = -1222730000,
        .temperature_c_x10 = 245,
        .altitude_m_x10 = 110,
        .hdop_x100 = 90,
        .speed_mps_x100 = 0,
        .course_deg_x100 = 0,
        .fix_age_ms = 0,
        .utc_time_ms = 45319000,
        .utc_date_ddmmyy = 10626,
        .fix_type = 3,
        .satellites = 8,
        .gnss_flags = GNSS_FIX_FLAG_GGA | GNSS_FIX_FLAG_RMC | GNSS_FIX_FLAG_CHECKSUM,
    };

    return telemetry_protocol_build(&sample, pkt);
}
