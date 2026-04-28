#include "telemetry_protocol.h"

#include <string.h>

esp_err_t telemetry_protocol_build(hope_packet_t *pkt) {
    memset(pkt, 0, sizeof(*pkt));

    pkt->version = 1;
    pkt->type = 1;

    pkt->src_id = 1;
    pkt->dst_id = 2;

    pkt->session_id = 0x12345678;
    pkt->timestamp = 0;

    const char *msg = "hello from ENDURANCE";
    pkt->payload_len = strlen(msg);
    memcpy(pkt->payload, msg, pkt->payload_len);

    return ESP_OK;
}