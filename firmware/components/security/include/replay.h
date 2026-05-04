#pragma once

#include <stdbool.h>
#include <stdint.h>

void replay_init(void);
bool replay_check_and_update(uint32_t counter);
bool replay_check_session_and_update(uint32_t session_id, uint32_t counter);
bool replay_restore_session_counter(uint32_t session_id, uint32_t largest_seen_counter);
