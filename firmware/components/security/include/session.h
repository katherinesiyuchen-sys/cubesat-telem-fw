#pragma once

#include <stdint.h>

void session_init(uint32_t session_id);
uint32_t session_get_id(void);
uint32_t session_next_counter(void);
