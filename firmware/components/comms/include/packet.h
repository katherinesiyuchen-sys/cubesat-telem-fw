#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define HOPE_PACKET_VERSION      1
#define HOPE_MAX_PAYLOAD_LEN     128
#define HOPE_PACKET_HEADER_LEN   20
#define HOPE_MAX_PACKET_LEN      (HOPE_PACKET_HEADER_LEN + HOPE_MAX_PAYLOAD_LEN)

typedef enum {
    HOPE_PACKET_TYPE_TELEMETRY = 1,
    HOPE_PACKET_TYPE_ALERT     = 2,
    HOPE_PACKET_TYPE_HANDSHAKE = 3,
    HOPE_PACKET_TYPE_ACK       = 4,
} hope_packet_type_t;

typedef struct {
    uint8_t  version; // protocol version, currently always 1
    uint8_t  type; //telemetry, alert, handshake, etc

    uint16_t src_id; //who sent it
    uint16_t dst_id; // who should receive it

    uint32_t session_id; // current session secure id
    uint32_t counter; // packet counter
    uint32_t timestamp; // packet timestamp

    uint8_t  payload[HOPE_MAX_PAYLOAD_LEN]; // actual data 
    size_t   payload_len;
} hope_packet_t;

void packet_init(hope_packet_t *pkt, hope_packet_type_t type, uint16_t src_id, uint16_t dst_id);
bool packet_type_is_valid(uint8_t type);
bool packet_header_is_valid(const hope_packet_t *pkt);
