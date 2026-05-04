#include "telemetry_protocol.h"

#include <string.h>

static void write_i16_be(uint8_t *buf, int16_t value) {
    uint16_t raw = (uint16_t)value;
    buf[0] = (uint8_t)((raw >> 8) & 0xFF);
    buf[1] = (uint8_t)(raw & 0xFF);
}

static void write_i32_be(uint8_t *buf, int32_t value) {
    uint32_t raw = (uint32_t)value;
    buf[0] = (uint8_t)((raw >> 24) & 0xFF);
    buf[1] = (uint8_t)((raw >> 16) & 0xFF);
    buf[2] = (uint8_t)((raw >> 8) & 0xFF);
    buf[3] = (uint8_t)(raw & 0xFF);
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
        .fix_type = fix->fix_type,
        .satellites = fix->satellites,
    };

    return telemetry_protocol_build(&sample, pkt);
}

esp_err_t telemetry_protocol_build_fake(hope_packet_t *pkt) {
    telemetry_sample_t sample = {
        .latitude_e7 = 378715000,
        .longitude_e7 = -1222730000,
        .temperature_c_x10 = 245,
        .fix_type = 3,
        .satellites = 8,
    };

    return telemetry_protocol_build(&sample, pkt);
}
