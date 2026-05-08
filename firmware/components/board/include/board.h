#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool expander_present;
    uint8_t porta_state;
    uint8_t portb_state;
    bool battery_alert_level;
    bool mag_drdy_level;
    bool power_alert_level;
    bool temp_alert_level;
    bool solar_alert_level;
    bool usb_power_alert_level;
} board_alert_snapshot_t;

typedef struct {
    int sda_pin;
    int scl_pin;
    uint32_t clock_hz;
    bool swapped_from_schematic;
} board_i2c_bus_snapshot_t;

// Header for board startup and initialization functions.
void board_init(void);
esp_err_t board_get_alert_snapshot(board_alert_snapshot_t *out_snapshot);
esp_err_t board_get_i2c_bus_snapshot(board_i2c_bus_snapshot_t *out_snapshot);

