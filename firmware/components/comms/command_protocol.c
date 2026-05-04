#include "command_protocol.h"

#include <stdbool.h>
#include <string.h>

#define COMMAND_REQUEST_MIN_LEN 26
#define COMMAND_REQUEST_ARG_OFFSET 26
#define COMMAND_ACK_MIN_LEN     10

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
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static uint32_t read_u32_be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) |
        ((uint32_t)buf[1] << 16) |
        ((uint32_t)buf[2] << 8) |
        (uint32_t)buf[3];
}

static bool opcode_is_valid(uint8_t opcode) {
    switch ((command_opcode_t)opcode) {
        case COMMAND_OPCODE_SELF_TEST:
        case COMMAND_OPCODE_PING:
        case COMMAND_OPCODE_TELEMETRY_NOW:
        case COMMAND_OPCODE_PAUSE_TELEMETRY:
        case COMMAND_OPCODE_RESUME_TELEMETRY:
        case COMMAND_OPCODE_ROTATE_SESSION:
        case COMMAND_OPCODE_OPEN_DOWNLINK:
        case COMMAND_OPCODE_ISOLATE:
        case COMMAND_OPCODE_CONNECT:
        case COMMAND_OPCODE_ARM:
            return true;
        default:
            return false;
    }
}

esp_err_t command_protocol_build_request_payload(
    const command_request_t *request,
    uint8_t *out_payload,
    size_t out_len,
    size_t *written
) {
    if (request == NULL || out_payload == NULL || written == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!opcode_is_valid(request->opcode) || request->arg_len > COMMAND_MAX_ARG_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t total_len = COMMAND_REQUEST_MIN_LEN + request->arg_len;
    if (out_len < total_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    out_payload[0] = COMMAND_PAYLOAD_VERSION;
    write_u32_be(&out_payload[1], request->command_id);
    out_payload[5] = request->opcode;
    out_payload[6] = request->flags;
    write_u16_be(&out_payload[7], request->auth_key_id);
    memcpy(&out_payload[9], request->auth_tag, COMMAND_AUTH_TAG_LEN);
    out_payload[25] = request->arg_len;
    if (request->arg_len > 0) {
        memcpy(&out_payload[COMMAND_REQUEST_ARG_OFFSET], request->arg, request->arg_len);
    }

    *written = total_len;
    return ESP_OK;
}

esp_err_t command_protocol_parse_request_payload(
    const uint8_t *payload,
    size_t payload_len,
    command_request_t *out_request
) {
    if (payload == NULL || out_request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len < COMMAND_REQUEST_MIN_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (payload[0] != COMMAND_PAYLOAD_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }

    uint8_t arg_len = payload[25];
    if (arg_len > COMMAND_MAX_ARG_LEN || payload_len < COMMAND_REQUEST_MIN_LEN + arg_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!opcode_is_valid(payload[5])) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    memset(out_request, 0, sizeof(*out_request));
    out_request->version = payload[0];
    out_request->command_id = read_u32_be(&payload[1]);
    out_request->opcode = payload[5];
    out_request->flags = payload[6];
    out_request->auth_key_id = read_u16_be(&payload[7]);
    memcpy(out_request->auth_tag, &payload[9], COMMAND_AUTH_TAG_LEN);
    out_request->arg_len = arg_len;
    if (arg_len > 0) {
        memcpy(out_request->arg, &payload[COMMAND_REQUEST_ARG_OFFSET], arg_len);
    }

    return ESP_OK;
}

esp_err_t command_protocol_build_request_packet(
    const command_request_t *request,
    uint16_t src_id,
    uint16_t dst_id,
    uint32_t session_id,
    uint32_t counter,
    uint32_t timestamp,
    hope_packet_t *out_packet
) {
    if (out_packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    packet_init(out_packet, HOPE_PACKET_TYPE_COMMAND, src_id, dst_id);
    out_packet->session_id = session_id;
    out_packet->counter = counter;
    out_packet->timestamp = timestamp;

    return command_protocol_build_request_payload(
        request,
        out_packet->payload,
        sizeof(out_packet->payload),
        &out_packet->payload_len
    );
}

esp_err_t command_protocol_build_ack_payload(
    const command_ack_t *ack,
    uint8_t *out_payload,
    size_t out_len,
    size_t *written
) {
    if (ack == NULL || out_payload == NULL || written == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ack->message_len > COMMAND_ACK_MAX_MESSAGE_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t total_len = COMMAND_ACK_MIN_LEN + ack->message_len;
    if (out_len < total_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    out_payload[0] = COMMAND_PAYLOAD_VERSION;
    out_payload[1] = ack->acked_type;
    write_u32_be(&out_payload[2], ack->command_id);
    out_payload[6] = ack->status;
    write_u16_be(&out_payload[7], (uint16_t)ack->detail_code);
    out_payload[9] = ack->message_len;
    if (ack->message_len > 0) {
        memcpy(&out_payload[10], ack->message, ack->message_len);
    }

    *written = total_len;
    return ESP_OK;
}

esp_err_t command_protocol_parse_ack_payload(
    const uint8_t *payload,
    size_t payload_len,
    command_ack_t *out_ack
) {
    if (payload == NULL || out_ack == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len < COMMAND_ACK_MIN_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (payload[0] != COMMAND_PAYLOAD_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }

    uint8_t message_len = payload[9];
    if (message_len > COMMAND_ACK_MAX_MESSAGE_LEN || payload_len < COMMAND_ACK_MIN_LEN + message_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(out_ack, 0, sizeof(*out_ack));
    out_ack->version = payload[0];
    out_ack->acked_type = payload[1];
    out_ack->command_id = read_u32_be(&payload[2]);
    out_ack->status = payload[6];
    out_ack->detail_code = (int16_t)read_u16_be(&payload[7]);
    out_ack->message_len = message_len;
    if (message_len > 0) {
        memcpy(out_ack->message, &payload[10], message_len);
    }
    out_ack->message[message_len] = '\0';

    return ESP_OK;
}

esp_err_t command_protocol_build_ack_packet(
    const command_ack_t *ack,
    uint16_t src_id,
    uint16_t dst_id,
    uint32_t session_id,
    uint32_t counter,
    uint32_t timestamp,
    hope_packet_t *out_packet
) {
    if (out_packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    packet_init(out_packet, HOPE_PACKET_TYPE_ACK, src_id, dst_id);
    out_packet->session_id = session_id;
    out_packet->counter = counter;
    out_packet->timestamp = timestamp;

    return command_protocol_build_ack_payload(
        ack,
        out_packet->payload,
        sizeof(out_packet->payload),
        &out_packet->payload_len
    );
}

const char *command_protocol_opcode_name(uint8_t opcode) {
    switch ((command_opcode_t)opcode) {
        case COMMAND_OPCODE_SELF_TEST:
            return "selftest";
        case COMMAND_OPCODE_PING:
            return "ping";
        case COMMAND_OPCODE_TELEMETRY_NOW:
            return "telemetry-now";
        case COMMAND_OPCODE_PAUSE_TELEMETRY:
            return "pause";
        case COMMAND_OPCODE_RESUME_TELEMETRY:
            return "resume";
        case COMMAND_OPCODE_ROTATE_SESSION:
            return "rotate-session";
        case COMMAND_OPCODE_OPEN_DOWNLINK:
            return "open-downlink";
        case COMMAND_OPCODE_ISOLATE:
            return "isolate";
        case COMMAND_OPCODE_CONNECT:
            return "connect";
        case COMMAND_OPCODE_ARM:
            return "arm";
        default:
            return "unknown";
    }
}
