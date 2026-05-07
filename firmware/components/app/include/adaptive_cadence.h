#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CUBESAT_CADENCE_MODE_AUTO = 0,
    CUBESAT_CADENCE_MODE_FAST,
    CUBESAT_CADENCE_MODE_NORMAL,
    CUBESAT_CADENCE_MODE_SLOW,
} cubesat_cadence_mode_t;

typedef enum {
    CUBESAT_CADENCE_CLASS_NOMINAL = 0,
    CUBESAT_CADENCE_CLASS_WATCH,
    CUBESAT_CADENCE_CLASS_PRIORITY,
    CUBESAT_CADENCE_CLASS_CONSERVE,
    CUBESAT_CADENCE_CLASS_MANUAL_FAST,
    CUBESAT_CADENCE_CLASS_MANUAL_SLOW,
} cubesat_cadence_class_t;

typedef struct {
    uint8_t link_margin_percent;
    uint8_t battery_percent;
    uint8_t risk_percent;
    int16_t temperature_c_x10;
    uint8_t satellites;
    uint16_t hdop_x100;
    uint32_t fix_age_ms;
    bool alert_active;
    bool bench_fix;
} cubesat_cadence_input_t;

typedef struct {
    uint32_t interval_ms;
    uint32_t target_interval_ms;
    uint8_t anomaly_score;
    cubesat_cadence_class_t classifier;
    const char *reason;
} cubesat_cadence_state_t;

void adaptive_cadence_init(cubesat_cadence_mode_t mode);
void adaptive_cadence_set_mode(cubesat_cadence_mode_t mode);
cubesat_cadence_mode_t adaptive_cadence_mode(void);
const cubesat_cadence_state_t *adaptive_cadence_update(const cubesat_cadence_input_t *input);
const char *adaptive_cadence_class_name(cubesat_cadence_class_t classifier);
const char *adaptive_cadence_mode_name(cubesat_cadence_mode_t mode);
