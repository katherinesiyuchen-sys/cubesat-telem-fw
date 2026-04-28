#include "packet_codec.h"

#include <string.h>

static void write_u16_be(uint8_t *buf, uint16_t value) {
    buf[0] = (uint8_t)((value >> 8) & 0xFF);
    buf[1] = (uint8_t)(value & 0xFF);
}

static void write_u32_be(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)((value >> 24) & 0xFF);
    buf[1] = (uint8_t)((value >> 16) & 0xFF);
    buf[2] = (uint8_t)((value >> 8) & 0xFF);
    buf[3] = (uint8_t)(value & 0xFF);
}

static uint16_t read_u16_be(const uint8_t *buf) {
    return ((uint16_t)buf[0] << 8) | ((uint16_t)buf[1]);
}

static uint32_t read_u32_be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) |
        ((uint32_t)buf[1] << 16) |
        ((uint32_t)buf[2] << 8)  |
        ((uint32_t)buf[3]);
}

int packet_encode(const hope_packet_t *pkt, uint8_t *out_buf, size_t out_len) {
    if (pkt == NULL || out_buf == NULL) {
        return -1;
    }

    if (pkt->payload_len > HOPE_MAX_PAYLOAD_LEN) {
        return -2;
    }

    size_t total_len = HOPE_PACKET_HEADER_LEN + pkt->payload_len;

    if (out_len < total_len) {
        return -3;
    }

    out_buf[0] = pkt->version;
    out_buf[1] = pkt->type;

    write_u16_be(&out_buf[2], pkt->src_id);
    write_u16_be(&out_buf[4], pkt->dst_id);

    write_u32_be(&out_buf[6], pkt->session_id);
    write_u32_be(&out_buf[10], pkt->counter);
    write_u32_be(&out_buf[14], pkt->timestamp);

    write_u16_be(&out_buf[18], (uint16_t)pkt->payload_len);

    memcpy(&out_buf[20], pkt->payload, pkt->payload_len);

    return (int)total_len;
}

int packet_decode(const uint8_t *buf, size_t len, hope_packet_t *out_pkt) {
    if (buf == NULL || out_pkt == NULL) {
        return -1;
    }

    if (len < HOPE_PACKET_HEADER_LEN) {
        return -2;
    }

    memset(out_pkt, 0, sizeof(*out_pkt));

    out_pkt->version = buf[0];
    out_pkt->type = buf[1];

    out_pkt->src_id = read_u16_be(&buf[2]);
    out_pkt->dst_id = read_u16_be(&buf[4]);

    out_pkt->session_id = read_u32_be(&buf[6]);
    out_pkt->counter = read_u32_be(&buf[10]);
    out_pkt->timestamp = read_u32_be(&buf[14]);

    uint16_t payload_len = read_u16_be(&buf[18]);

    if (payload_len > HOPE_MAX_PAYLOAD_LEN) {
        return -3;
    }

    if (len < HOPE_PACKET_HEADER_LEN + payload_len) {
        return -4;
    }

    out_pkt->payload_len = payload_len;
    memcpy(out_pkt->payload, &buf[20], payload_len);

    return 0;
}