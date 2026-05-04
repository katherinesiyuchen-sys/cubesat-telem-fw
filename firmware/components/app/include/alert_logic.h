#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CUBESAT_ALERT_NONE = 0,
    CUBESAT_ALERT_INFO,
    CUBESAT_ALERT_WARN,
    CUBESAT_ALERT_CRITICAL,
} cubesat_alert_level_t;

typedef struct {
    int16_t temperature_c_x10;
    uint8_t satellites;
    uint8_t fix_type;
    int16_t link_margin_db_x10;
    bool replay_rejected;
} cubesat_alert_input_t;

typedef struct {
    cubesat_alert_level_t level;
    const char *message;
} cubesat_alert_result_t;

cubesat_alert_result_t alert_logic_evaluate(const cubesat_alert_input_t *input);
