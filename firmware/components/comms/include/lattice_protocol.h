#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "packet.h"

#define LATTICE_PAYLOAD_VERSION          1
#define LATTICE_FRAGMENT_HEADER_LEN      12
#define LATTICE_FRAGMENT_DATA_MAX        (HOPE_MAX_PAYLOAD_LEN - LATTICE_FRAGMENT_HEADER_LEN)
#define LATTICE_MAX_OBJECT_LEN           2420
#define LATTICE_MAX_FRAGMENT_COUNT       ((LATTICE_MAX_OBJECT_LEN + LATTICE_FRAGMENT_DATA_MAX - 1) / LATTICE_FRAGMENT_DATA_MAX)
#define LATTICE_REASSEMBLY_MAP_BYTES     ((LATTICE_MAX_FRAGMENT_COUNT + 7) / 8)

typedef enum {
    LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY = 1,
    LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY = 2,
    LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT = 3,
    LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY = 4,
    LATTICE_MSG_SESSION_CONFIRM = 5,
    LATTICE_MSG_STATUS = 6,
    LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE = 7,
    LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE = 8,
} lattice_message_type_t;

typedef struct {
    uint8_t version;
    uint8_t message_type;
    uint16_t transfer_id;
    uint16_t fragment_index;
    uint16_t fragment_count;
    uint16_t total_len;
    uint16_t fragment_len;
    const uint8_t *fragment;
} lattice_fragment_t;

typedef struct {
    bool active;
    uint8_t message_type;
    uint16_t transfer_id;
    uint16_t fragment_count;
    uint16_t total_len;
    uint16_t received_count;
    uint8_t received_map[LATTICE_REASSEMBLY_MAP_BYTES];
    uint8_t data[LATTICE_MAX_OBJECT_LEN];
} lattice_reassembly_t;

bool lattice_protocol_message_type_is_valid(uint8_t message_type);
uint16_t lattice_protocol_fragment_count(size_t total_len);

esp_err_t lattice_protocol_build_fragment_payload(
    uint8_t message_type,
    uint16_t transfer_id,
    uint16_t fragment_index,
    const uint8_t *object,
    size_t object_len,
    uint8_t *out_payload,
    size_t out_len,
    size_t *written
);

esp_err_t lattice_protocol_parse_fragment_payload(
    const uint8_t *payload,
    size_t payload_len,
    lattice_fragment_t *out_fragment
);

esp_err_t lattice_protocol_build_fragment_packet(
    uint8_t message_type,
    uint16_t transfer_id,
    uint16_t fragment_index,
    const uint8_t *object,
    size_t object_len,
    uint16_t src_id,
    uint16_t dst_id,
    uint32_t session_id,
    uint32_t counter,
    uint32_t timestamp,
    hope_packet_t *out_packet
);

void lattice_reassembly_reset(lattice_reassembly_t *reassembly);

esp_err_t lattice_reassembly_add(
    lattice_reassembly_t *reassembly,
    const lattice_fragment_t *fragment,
    bool *complete
);

const uint8_t *lattice_reassembly_data(const lattice_reassembly_t *reassembly);
size_t lattice_reassembly_len(const lattice_reassembly_t *reassembly);
uint8_t lattice_reassembly_message_type(const lattice_reassembly_t *reassembly);
uint16_t lattice_reassembly_transfer_id(const lattice_reassembly_t *reassembly);

const char *lattice_protocol_message_name(uint8_t message_type);
