#pragma once

#include <stddef.h>
#include <stdint.h>

#define HOPE_MAX_PAYLOAD_LEN 128

typedef struct {
    uint8_t version;
    uint8_t type;

    uint16_t src_id;
    uint16_t dst_id;

    uint32_t session_id;
    uint32_t counter;
    uint32_t timestamp;

    uint8_t payload[HOPE_MAX_PAYLOAD_LEN];
    size_t payload_len;
} hope_packet_t;


//base packet format 