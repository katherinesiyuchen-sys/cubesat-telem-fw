#include "session.h"

static uint32_t s_session_id = 0;
static uint32_t s_send_counter = 0;

void session_init(uint32_t session_id) {
    if (s_session_id == session_id) {
        return;
    }
    session_init_with_counter(session_id, 0);
}

void session_init_with_counter(uint32_t session_id, uint32_t send_counter) {
    s_session_id = session_id;
    s_send_counter = send_counter;
}

uint32_t session_get_id(void) {
    return s_session_id;
}

uint32_t session_get_counter(void) {
    return s_send_counter;
}

uint32_t session_next_counter(void) {
    s_send_counter++;
    return s_send_counter;
}
