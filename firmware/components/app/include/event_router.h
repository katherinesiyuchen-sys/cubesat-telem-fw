#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "packet.h"

typedef enum {
    EVENT_ROUTER_ACCEPTED = 0,
    EVENT_ROUTER_REPLAY_REJECTED,
    EVENT_ROUTER_PARSE_ERROR,
} event_router_status_t;

typedef struct {
    event_router_status_t status;
    hope_packet_t packet;
} event_router_result_t;

void event_router_init(void);
event_router_result_t event_router_handle_raw_packet(const uint8_t *data, size_t len);
