#include "lattice_protocol.h"

#include <string.h>

static void write_u16_be(uint8_t *buf, uint16_t value) {
    buf[0] = (uint8_t)((value >> 8) & 0xFF);
    buf[1] = (uint8_t)(value & 0xFF);
}

static uint16_t read_u16_be(const uint8_t *buf) {
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static void mark_received(lattice_reassembly_t *reassembly, uint16_t index) {
    reassembly->received_map[index / 8U] |= (uint8_t)(1U << (index % 8U));
}

static bool is_received(const lattice_reassembly_t *reassembly, uint16_t index) {
    return (reassembly->received_map[index / 8U] & (uint8_t)(1U << (index % 8U))) != 0;
}

bool lattice_protocol_message_type_is_valid(uint8_t message_type) {
    switch ((lattice_message_type_t)message_type) {
        case LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY:
        case LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY:
        case LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT:
        case LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY:
        case LATTICE_MSG_SESSION_CONFIRM:
        case LATTICE_MSG_STATUS:
        case LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE:
        case LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE:
            return true;
        default:
            return false;
    }
}

uint16_t lattice_protocol_fragment_count(size_t total_len) {
    if (total_len == 0 || total_len > LATTICE_MAX_OBJECT_LEN) {
        return 0;
    }
    return (uint16_t)((total_len + LATTICE_FRAGMENT_DATA_MAX - 1U) / LATTICE_FRAGMENT_DATA_MAX);
}

esp_err_t lattice_protocol_build_fragment_payload(
    uint8_t message_type,
    uint16_t transfer_id,
    uint16_t fragment_index,
    const uint8_t *object,
    size_t object_len,
    uint8_t *out_payload,
    size_t out_len,
    size_t *written
) {
    if (object == NULL || out_payload == NULL || written == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!lattice_protocol_message_type_is_valid(message_type) || object_len == 0 || object_len > LATTICE_MAX_OBJECT_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t fragment_count = lattice_protocol_fragment_count(object_len);
    if (fragment_count == 0 || fragment_index >= fragment_count) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t offset = (size_t)fragment_index * LATTICE_FRAGMENT_DATA_MAX;
    size_t fragment_len = object_len - offset;
    if (fragment_len > LATTICE_FRAGMENT_DATA_MAX) {
        fragment_len = LATTICE_FRAGMENT_DATA_MAX;
    }

    size_t total_payload_len = LATTICE_FRAGMENT_HEADER_LEN + fragment_len;
    if (out_len < total_payload_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    out_payload[0] = LATTICE_PAYLOAD_VERSION;
    out_payload[1] = message_type;
    write_u16_be(&out_payload[2], transfer_id);
    write_u16_be(&out_payload[4], fragment_index);
    write_u16_be(&out_payload[6], fragment_count);
    write_u16_be(&out_payload[8], (uint16_t)object_len);
    write_u16_be(&out_payload[10], (uint16_t)fragment_len);
    memcpy(&out_payload[LATTICE_FRAGMENT_HEADER_LEN], &object[offset], fragment_len);

    *written = total_payload_len;
    return ESP_OK;
}

esp_err_t lattice_protocol_parse_fragment_payload(
    const uint8_t *payload,
    size_t payload_len,
    lattice_fragment_t *out_fragment
) {
    if (payload == NULL || out_fragment == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len < LATTICE_FRAGMENT_HEADER_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (payload[0] != LATTICE_PAYLOAD_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    if (!lattice_protocol_message_type_is_valid(payload[1])) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint16_t fragment_index = read_u16_be(&payload[4]);
    uint16_t fragment_count = read_u16_be(&payload[6]);
    uint16_t total_len = read_u16_be(&payload[8]);
    uint16_t fragment_len = read_u16_be(&payload[10]);

    if (total_len == 0 || total_len > LATTICE_MAX_OBJECT_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (fragment_count == 0 || fragment_count != lattice_protocol_fragment_count(total_len)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (fragment_index >= fragment_count || fragment_len == 0 || fragment_len > LATTICE_FRAGMENT_DATA_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (payload_len != LATTICE_FRAGMENT_HEADER_LEN + fragment_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = (size_t)fragment_index * LATTICE_FRAGMENT_DATA_MAX;
    size_t expected_len = total_len - offset;
    if (expected_len > LATTICE_FRAGMENT_DATA_MAX) {
        expected_len = LATTICE_FRAGMENT_DATA_MAX;
    }
    if (fragment_len != expected_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(out_fragment, 0, sizeof(*out_fragment));
    out_fragment->version = payload[0];
    out_fragment->message_type = payload[1];
    out_fragment->transfer_id = read_u16_be(&payload[2]);
    out_fragment->fragment_index = fragment_index;
    out_fragment->fragment_count = fragment_count;
    out_fragment->total_len = total_len;
    out_fragment->fragment_len = fragment_len;
    out_fragment->fragment = &payload[LATTICE_FRAGMENT_HEADER_LEN];
    return ESP_OK;
}

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
) {
    if (out_packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    packet_init(out_packet, HOPE_PACKET_TYPE_HANDSHAKE, src_id, dst_id);
    out_packet->session_id = session_id;
    out_packet->counter = counter;
    out_packet->timestamp = timestamp;

    return lattice_protocol_build_fragment_payload(
        message_type,
        transfer_id,
        fragment_index,
        object,
        object_len,
        out_packet->payload,
        sizeof(out_packet->payload),
        &out_packet->payload_len
    );
}

void lattice_reassembly_reset(lattice_reassembly_t *reassembly) {
    if (reassembly == NULL) {
        return;
    }
    memset(reassembly, 0, sizeof(*reassembly));
}

esp_err_t lattice_reassembly_add(
    lattice_reassembly_t *reassembly,
    const lattice_fragment_t *fragment,
    bool *complete
) {
    if (reassembly == NULL || fragment == NULL || complete == NULL || fragment->fragment == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (fragment->fragment_index >= LATTICE_MAX_FRAGMENT_COUNT || fragment->fragment_count > LATTICE_MAX_FRAGMENT_COUNT) {
        return ESP_ERR_INVALID_SIZE;
    }

    *complete = false;

    if (
        !reassembly->active ||
        reassembly->message_type != fragment->message_type ||
        reassembly->transfer_id != fragment->transfer_id ||
        reassembly->total_len != fragment->total_len ||
        reassembly->fragment_count != fragment->fragment_count
    ) {
        lattice_reassembly_reset(reassembly);
        reassembly->active = true;
        reassembly->message_type = fragment->message_type;
        reassembly->transfer_id = fragment->transfer_id;
        reassembly->total_len = fragment->total_len;
        reassembly->fragment_count = fragment->fragment_count;
    }

    if (is_received(reassembly, fragment->fragment_index)) {
        *complete = reassembly->received_count == reassembly->fragment_count;
        return ESP_OK;
    }

    size_t offset = (size_t)fragment->fragment_index * LATTICE_FRAGMENT_DATA_MAX;
    if (offset + fragment->fragment_len > sizeof(reassembly->data)) {
        lattice_reassembly_reset(reassembly);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(&reassembly->data[offset], fragment->fragment, fragment->fragment_len);
    mark_received(reassembly, fragment->fragment_index);
    reassembly->received_count++;
    *complete = reassembly->received_count == reassembly->fragment_count;
    return ESP_OK;
}

const uint8_t *lattice_reassembly_data(const lattice_reassembly_t *reassembly) {
    if (reassembly == NULL || !reassembly->active || reassembly->received_count != reassembly->fragment_count) {
        return NULL;
    }
    return reassembly->data;
}

size_t lattice_reassembly_len(const lattice_reassembly_t *reassembly) {
    if (reassembly == NULL || !reassembly->active || reassembly->received_count != reassembly->fragment_count) {
        return 0;
    }
    return reassembly->total_len;
}

uint8_t lattice_reassembly_message_type(const lattice_reassembly_t *reassembly) {
    return reassembly == NULL ? 0 : reassembly->message_type;
}

uint16_t lattice_reassembly_transfer_id(const lattice_reassembly_t *reassembly) {
    return reassembly == NULL ? 0 : reassembly->transfer_id;
}

const char *lattice_protocol_message_name(uint8_t message_type) {
    switch ((lattice_message_type_t)message_type) {
        case LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY:
            return "node-mlkem-public-key";
        case LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY:
            return "node-mldsa-public-key";
        case LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT:
            return "ground-mlkem-ciphertext";
        case LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY:
            return "ground-mldsa-public-key";
        case LATTICE_MSG_SESSION_CONFIRM:
            return "session-confirm";
        case LATTICE_MSG_STATUS:
            return "status";
        case LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE:
            return "node-handshake-signature";
        case LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE:
            return "ground-handshake-signature";
        default:
            return "unknown";
    }
}
