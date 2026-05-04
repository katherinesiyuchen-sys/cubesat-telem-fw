#include "replay.h"

#include <stddef.h>

#define REPLAY_SESSION_SLOTS 8

typedef struct {
    uint32_t session_id;
    uint32_t largest_seen_counter;
    bool used;
} replay_slot_t;

static replay_slot_t s_slots[REPLAY_SESSION_SLOTS];

void replay_init(void) {
    for (size_t i = 0; i < REPLAY_SESSION_SLOTS; ++i) {
        s_slots[i] = (replay_slot_t){ 0 };
    }
}

bool replay_check_and_update(uint32_t counter) {
    return replay_check_session_and_update(0, counter);
}

bool replay_check_session_and_update(uint32_t session_id, uint32_t counter) {
    if (counter == 0) {
        return false;
    }

    replay_slot_t *candidate = NULL;
    for (size_t i = 0; i < REPLAY_SESSION_SLOTS; ++i) {
        if (s_slots[i].used && s_slots[i].session_id == session_id) {
            candidate = &s_slots[i];
            break;
        }
        if (!s_slots[i].used && candidate == NULL) {
            candidate = &s_slots[i];
        }
    }

    if (candidate == NULL) {
        return false;
    }

    if (!candidate->used) {
        candidate->used = true;
        candidate->session_id = session_id;
        candidate->largest_seen_counter = counter;
        return true;
    }

    if (counter <= candidate->largest_seen_counter) {
        return false;
    }

    candidate->largest_seen_counter = counter;
    return true;
}

bool replay_restore_session_counter(uint32_t session_id, uint32_t largest_seen_counter) {
    if (largest_seen_counter == 0) {
        return false;
    }

    replay_slot_t *candidate = NULL;
    for (size_t i = 0; i < REPLAY_SESSION_SLOTS; ++i) {
        if (s_slots[i].used && s_slots[i].session_id == session_id) {
            candidate = &s_slots[i];
            break;
        }
        if (!s_slots[i].used && candidate == NULL) {
            candidate = &s_slots[i];
        }
    }

    if (candidate == NULL) {
        return false;
    }

    candidate->used = true;
    candidate->session_id = session_id;
    candidate->largest_seen_counter = largest_seen_counter;
    return true;
}
