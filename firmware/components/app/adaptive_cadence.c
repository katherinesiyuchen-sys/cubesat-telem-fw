#include "adaptive_cadence.h"

#include <stddef.h>

#define CADENCE_MIN_INTERVAL_MS       2000UL
#define CADENCE_FAST_INTERVAL_MS      2500UL
#define CADENCE_WATCH_INTERVAL_MS     5000UL
#define CADENCE_NOMINAL_INTERVAL_MS   8000UL
#define CADENCE_BENCH_INTERVAL_MS     12000UL
#define CADENCE_SLOW_INTERVAL_MS      18000UL
#define CADENCE_CONSERVE_INTERVAL_MS  24000UL
#define CADENCE_MAX_INTERVAL_MS       30000UL

static cubesat_cadence_mode_t s_mode;
static cubesat_cadence_state_t s_state;
static uint8_t s_link_ma;
static uint8_t s_risk_ma;
static int16_t s_temp_ma_x10;
static bool s_initialized;

static uint8_t clamp_u8(uint32_t value, uint8_t max_value) {
    return value > max_value ? max_value : (uint8_t)value;
}

static uint32_t clamp_interval(uint32_t interval_ms) {
    if (interval_ms < CADENCE_MIN_INTERVAL_MS) {
        return CADENCE_MIN_INTERVAL_MS;
    }
    if (interval_ms > CADENCE_MAX_INTERVAL_MS) {
        return CADENCE_MAX_INTERVAL_MS;
    }
    return interval_ms;
}

static uint8_t low_pass_u8(uint8_t previous, uint8_t sample) {
    return (uint8_t)(((uint16_t)previous * 7U + sample) / 8U);
}

static int16_t low_pass_i16(int16_t previous, int16_t sample) {
    return (int16_t)(((int32_t)previous * 7 + sample) / 8);
}

static uint8_t compute_anomaly(const cubesat_cadence_input_t *input) {
    uint32_t score = input->risk_percent / 2U;

    if (s_link_ma < 55U) {
        score += (55U - s_link_ma) * 2U;
    }
    if (input->satellites < 5U) {
        score += (5U - input->satellites) * 9U;
    }
    if (input->hdop_x100 > 180U) {
        score += (input->hdop_x100 - 180U) / 4U;
    }
    if (input->fix_age_ms > 1500U) {
        score += (input->fix_age_ms - 1500U) / 80U;
    }
    if (s_temp_ma_x10 > 320) {
        score += (uint32_t)(s_temp_ma_x10 - 320) / 5U;
    }
    if (input->battery_percent < 25U) {
        score += (25U - input->battery_percent) * 3U;
    }
    if (input->alert_active) {
        score += 22U;
    }
    if (input->bench_fix) {
        score += 8U;
    }

    return clamp_u8(score, 100U);
}

static cubesat_cadence_class_t choose_auto_class(
    const cubesat_cadence_input_t *input,
    uint8_t anomaly,
    uint32_t *target_interval_ms,
    const char **reason
) {
    if (input->battery_percent < 16U) {
        *target_interval_ms = CADENCE_CONSERVE_INTERVAL_MS;
        *reason = "battery reserve";
        return CUBESAT_CADENCE_CLASS_CONSERVE;
    }
    if (anomaly >= 70U || input->alert_active) {
        *target_interval_ms = CADENCE_FAST_INTERVAL_MS;
        *reason = input->alert_active ? "active alert" : "anomaly high";
        return CUBESAT_CADENCE_CLASS_PRIORITY;
    }
    if (anomaly >= 45U || s_link_ma < 55U) {
        *target_interval_ms = CADENCE_WATCH_INTERVAL_MS;
        *reason = "moving average watch";
        return CUBESAT_CADENCE_CLASS_WATCH;
    }
    if (input->bench_fix) {
        *target_interval_ms = CADENCE_BENCH_INTERVAL_MS;
        *reason = "bench fallback";
        return CUBESAT_CADENCE_CLASS_NOMINAL;
    }

    *target_interval_ms = CADENCE_NOMINAL_INTERVAL_MS;
    *reason = "nominal";
    return CUBESAT_CADENCE_CLASS_NOMINAL;
}

void adaptive_cadence_init(cubesat_cadence_mode_t mode) {
    s_mode = mode;
    s_state = (cubesat_cadence_state_t){
        .interval_ms = CADENCE_NOMINAL_INTERVAL_MS,
        .target_interval_ms = CADENCE_NOMINAL_INTERVAL_MS,
        .anomaly_score = 0,
        .classifier = CUBESAT_CADENCE_CLASS_NOMINAL,
        .reason = "init",
    };
    s_link_ma = 75U;
    s_risk_ma = 20U;
    s_temp_ma_x10 = 220;
    s_initialized = true;
}

void adaptive_cadence_set_mode(cubesat_cadence_mode_t mode) {
    if (!s_initialized) {
        adaptive_cadence_init(mode);
        return;
    }
    s_mode = mode;
}

cubesat_cadence_mode_t adaptive_cadence_mode(void) {
    return s_mode;
}

const cubesat_cadence_state_t *adaptive_cadence_update(const cubesat_cadence_input_t *input) {
    if (!s_initialized) {
        adaptive_cadence_init(CUBESAT_CADENCE_MODE_AUTO);
    }
    if (input == NULL) {
        s_state.target_interval_ms = CADENCE_NOMINAL_INTERVAL_MS;
        s_state.interval_ms = CADENCE_NOMINAL_INTERVAL_MS;
        s_state.classifier = CUBESAT_CADENCE_CLASS_WATCH;
        s_state.anomaly_score = 50U;
        s_state.reason = "missing input";
        return &s_state;
    }

    s_link_ma = low_pass_u8(s_link_ma, input->link_margin_percent);
    s_risk_ma = low_pass_u8(s_risk_ma, input->risk_percent);
    s_temp_ma_x10 = low_pass_i16(s_temp_ma_x10, input->temperature_c_x10);

    uint8_t anomaly = compute_anomaly(input);
    uint32_t target = CADENCE_NOMINAL_INTERVAL_MS;
    const char *reason = "nominal";
    cubesat_cadence_class_t classifier = CUBESAT_CADENCE_CLASS_NOMINAL;

    switch (s_mode) {
        case CUBESAT_CADENCE_MODE_FAST:
            target = CADENCE_FAST_INTERVAL_MS;
            reason = "operator fast";
            classifier = CUBESAT_CADENCE_CLASS_MANUAL_FAST;
            break;
        case CUBESAT_CADENCE_MODE_NORMAL:
            target = CADENCE_NOMINAL_INTERVAL_MS;
            reason = "operator normal";
            classifier = CUBESAT_CADENCE_CLASS_NOMINAL;
            break;
        case CUBESAT_CADENCE_MODE_SLOW:
            target = CADENCE_SLOW_INTERVAL_MS;
            reason = "operator slow";
            classifier = CUBESAT_CADENCE_CLASS_MANUAL_SLOW;
            break;
        case CUBESAT_CADENCE_MODE_AUTO:
        default:
            classifier = choose_auto_class(input, anomaly, &target, &reason);
            break;
    }

    target = clamp_interval(target);
    uint32_t current = s_state.interval_ms == 0U ? CADENCE_NOMINAL_INTERVAL_MS : s_state.interval_ms;
    if (target > current) {
        current += (target - current + 7U) / 8U;
    } else {
        current -= (current - target + 3U) / 4U;
    }

    s_state.target_interval_ms = target;
    s_state.interval_ms = clamp_interval(current);
    s_state.anomaly_score = anomaly;
    s_state.classifier = classifier;
    s_state.reason = reason;
    (void)s_risk_ma;
    return &s_state;
}

const char *adaptive_cadence_class_name(cubesat_cadence_class_t classifier) {
    switch (classifier) {
        case CUBESAT_CADENCE_CLASS_NOMINAL:
            return "NOMINAL";
        case CUBESAT_CADENCE_CLASS_WATCH:
            return "WATCH";
        case CUBESAT_CADENCE_CLASS_PRIORITY:
            return "PRIORITY";
        case CUBESAT_CADENCE_CLASS_CONSERVE:
            return "CONSERVE";
        case CUBESAT_CADENCE_CLASS_MANUAL_FAST:
            return "MANUAL_FAST";
        case CUBESAT_CADENCE_CLASS_MANUAL_SLOW:
            return "MANUAL_SLOW";
        default:
            return "UNKNOWN";
    }
}

const char *adaptive_cadence_mode_name(cubesat_cadence_mode_t mode) {
    switch (mode) {
        case CUBESAT_CADENCE_MODE_AUTO:
            return "AUTO";
        case CUBESAT_CADENCE_MODE_FAST:
            return "FAST";
        case CUBESAT_CADENCE_MODE_NORMAL:
            return "NORMAL";
        case CUBESAT_CADENCE_MODE_SLOW:
            return "SLOW";
        default:
            return "UNKNOWN";
    }
}
