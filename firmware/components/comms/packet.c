#include "packet.h"

#include <string.h>

void packet_init(hope_packet_t *pkt, hope_packet_type_t type, uint16_t src_id, uint16_t dst_id) {
    if (pkt == NULL) {
        return;
    }

    memset(pkt, 0, sizeof(*pkt));
    pkt->version = HOPE_PACKET_VERSION;
    pkt->type = (uint8_t)type;
    pkt->src_id = src_id;
    pkt->dst_id = dst_id;
}

bool packet_type_is_valid(uint8_t type) {
    switch ((hope_packet_type_t)type) {
        case HOPE_PACKET_TYPE_TELEMETRY:
        case HOPE_PACKET_TYPE_ALERT:
        case HOPE_PACKET_TYPE_HANDSHAKE:
        case HOPE_PACKET_TYPE_ACK:
        case HOPE_PACKET_TYPE_DIAGNOSTIC:
        case HOPE_PACKET_TYPE_COMMAND:
            return true;
        default:
            return false;
    }
}

bool packet_header_is_valid(const hope_packet_t *pkt) {
    if (pkt == NULL) {
        return false;
    }
    if (pkt->version != HOPE_PACKET_VERSION || !packet_type_is_valid(pkt->type)) {
        return false;
    }
    return pkt->payload_len <= HOPE_MAX_PAYLOAD_LEN;
}
