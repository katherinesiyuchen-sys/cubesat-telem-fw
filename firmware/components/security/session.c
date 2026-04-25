#include "session.h"

static uint32_t s_send_counter = 0;

void session_init(void) {
    s_send_counter = 0;
}

uint32_t session_next_counter(void) {
    s_send_counter++;
    return s_send_counter;
}