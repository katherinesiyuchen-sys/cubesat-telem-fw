#include "event_router.h"

#include <string.h>

#include "app_state.h"
#include "packet_codec.h"
#include "replay.h"

void event_router_init(void) {
    replay_init();
}

event_router_result_t event_router_handle_raw_packet(const uint8_t *data, size_t len) {
    event_router_result_t result;
    memset(&result, 0, sizeof(result));

    int decode_result = packet_decode(data, len, &result.packet);
    if (decode_result != 0) {
        result.status = EVENT_ROUTER_PARSE_ERROR;
        app_state_record_parse_error();
        return result;
    }

    if (!replay_check_session_and_update(result.packet.session_id, result.packet.counter)) {
        result.status = EVENT_ROUTER_REPLAY_REJECTED;
        app_state_record_replay_reject();
        return result;
    }

    result.status = EVENT_ROUTER_ACCEPTED;
    app_state_record_rx(result.packet.counter);
    return result;
}
