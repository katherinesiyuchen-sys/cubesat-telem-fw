#include "power_monitor.h"

esp_err_t power_monitor_init(void) {
    return ESP_OK;
}

esp_err_t power_monitor_read(power_monitor_sample_t *sample) {
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sample->bus_mv = 3800;
    sample->current_ma = 120;
    sample->battery_percent = 90;
    return ESP_OK;
}
