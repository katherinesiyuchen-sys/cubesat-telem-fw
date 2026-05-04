#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "packet.h"

#define COMMAND_PAYLOAD_VERSION 1
#define COMMAND_MAX_ARG_LEN     48
#define COMMAND_AUTH_TAG_LEN    16
#define COMMAND_FLAG_AUTH_PRESENT 0x01
#define COMMAND_ACK_MAX_MESSAGE_LEN 48

typedef enum {
    COMMAND_OPCODE_SELF_TEST = 1,
    COMMAND_OPCODE_PING = 2,
    COMMAND_OPCODE_TELEMETRY_NOW = 3,
    COMMAND_OPCODE_PAUSE_TELEMETRY = 4,
    COMMAND_OPCODE_RESUME_TELEMETRY = 5,
    COMMAND_OPCODE_ROTATE_SESSION = 6,
    COMMAND_OPCODE_OPEN_DOWNLINK = 7,
    COMMAND_OPCODE_ISOLATE = 8,
    COMMAND_OPCODE_CONNECT = 9,
    COMMAND_OPCODE_ARM = 10,
} command_opcode_t;

typedef enum {
    COMMAND_ACK_STATUS_OK = 0,
    COMMAND_ACK_STATUS_REJECTED = 1,
    COMMAND_ACK_STATUS_ERROR = 2,
} command_ack_status_t;

typedef struct {
    uint8_t version;
    uint32_t command_id;
    uint8_t opcode;
    uint8_t flags;
    uint16_t auth_key_id;
    uint8_t auth_tag[COMMAND_AUTH_TAG_LEN];
    uint8_t arg_len;
    uint8_t arg[COMMAND_MAX_ARG_LEN];
} command_request_t;

typedef struct {
    uint8_t version;
    uint8_t acked_type;
    uint32_t command_id;
    uint8_t status;
    int16_t detail_code;
    uint8_t message_len;
    char message[COMMAND_ACK_MAX_MESSAGE_LEN + 1];
} command_ack_t;

esp_err_t command_protocol_build_request_payload(
    const command_request_t *request,
    uint8_t *out_payload,
    size_t out_len,
    size_t *written
);

esp_err_t command_protocol_parse_request_payload(
    const uint8_t *payload,
    size_t payload_len,
    command_request_t *out_request
);

esp_err_t command_protocol_build_request_packet(
    const command_request_t *request,
    uint16_t src_id,
    uint16_t dst_id,
    uint32_t session_id,
    uint32_t counter,
    uint32_t timestamp,
    hope_packet_t *out_packet
);

esp_err_t command_protocol_build_ack_payload(
    const command_ack_t *ack,
    uint8_t *out_payload,
    size_t out_len,
    size_t *written
);

esp_err_t command_protocol_parse_ack_payload(
    const uint8_t *payload,
    size_t payload_len,
    command_ack_t *out_ack
);

esp_err_t command_protocol_build_ack_packet(
    const command_ack_t *ack,
    uint16_t src_id,
    uint16_t dst_id,
    uint32_t session_id,
    uint32_t counter,
    uint32_t timestamp,
    hope_packet_t *out_packet
);

const char *command_protocol_opcode_name(uint8_t opcode);
