#include "diagnostic_protocol.h"

#include <string.h>

static void write_u16_be(uint8_t *buf, uint16_t value) {
    buf[0] = (uint8_t)((value >> 8) & 0xFF);
    buf[1] = (uint8_t)(value & 0xFF);
}

static void write_i16_be(uint8_t *buf, int16_t value) {
    write_u16_be(buf, (uint16_t)value);
}

static void write_u32_be(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)((value >> 24) & 0xFF);
    buf[1] = (uint8_t)((value >> 16) & 0xFF);
    buf[2] = (uint8_t)((value >> 8) & 0xFF);
    buf[3] = (uint8_t)(value & 0xFF);
}

static uint16_t read_u16_be(const uint8_t *buf) {
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static int16_t read_i16_be(const uint8_t *buf) {
    return (int16_t)read_u16_be(buf);
}

static uint32_t read_u32_be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) |
        ((uint32_t)buf[1] << 16) |
        ((uint32_t)buf[2] << 8) |
        (uint32_t)buf[3];
}

esp_err_t diagnostic_protocol_build_payload(
    const diagnostic_report_t *report,
    uint8_t *payload,
    size_t payload_len
) {
    if (report == NULL || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len < DIAGNOSTIC_PAYLOAD_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(payload, 0, payload_len);
    payload[0] = report->version;
    payload[1] = report->overall_status;
    write_u16_be(&payload[2], report->passed_mask);
    write_u16_be(&payload[4], report->warning_mask);
    write_u16_be(&payload[6], report->failed_mask);
    write_u32_be(&payload[8], report->uptime_s);
    write_u32_be(&payload[12], report->boot_count);
    write_i16_be(&payload[16], report->lora_status);
    write_i16_be(&payload[18], report->gnss_status);
    write_i16_be(&payload[20], report->i2c_status);
    write_i16_be(&payload[22], report->rng_status);
    write_i16_be(&payload[24], report->nvs_status);
    payload[26] = report->i2c_devices_seen;
    memcpy(&payload[27], report->pins, DIAGNOSTIC_PIN_COUNT);
    return ESP_OK;
}

esp_err_t diagnostic_protocol_parse_payload(
    const uint8_t *payload,
    size_t payload_len,
    diagnostic_report_t *report
) {
    if (payload == NULL || report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len != DIAGNOSTIC_PAYLOAD_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(report, 0, sizeof(*report));
    report->version = payload[0];
    report->overall_status = payload[1];
    report->passed_mask = read_u16_be(&payload[2]);
    report->warning_mask = read_u16_be(&payload[4]);
    report->failed_mask = read_u16_be(&payload[6]);
    report->uptime_s = read_u32_be(&payload[8]);
    report->boot_count = read_u32_be(&payload[12]);
    report->lora_status = read_i16_be(&payload[16]);
    report->gnss_status = read_i16_be(&payload[18]);
    report->i2c_status = read_i16_be(&payload[20]);
    report->rng_status = read_i16_be(&payload[22]);
    report->nvs_status = read_i16_be(&payload[24]);
    report->i2c_devices_seen = payload[26];
    memcpy(report->pins, &payload[27], DIAGNOSTIC_PIN_COUNT);
    return report->version == DIAGNOSTIC_PAYLOAD_VERSION ? ESP_OK : ESP_ERR_INVALID_VERSION;
}

esp_err_t diagnostic_protocol_build_packet(
    const diagnostic_report_t *report,
    hope_packet_t *pkt
) {
    if (report == NULL || pkt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(pkt, 0, sizeof(*pkt));
    pkt->version = HOPE_PACKET_VERSION;
    pkt->type = HOPE_PACKET_TYPE_DIAGNOSTIC;
    pkt->payload_len = DIAGNOSTIC_PAYLOAD_LEN;
    return diagnostic_protocol_build_payload(report, pkt->payload, sizeof(pkt->payload));
}
