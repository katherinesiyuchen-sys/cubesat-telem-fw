#include "temp_sensor.h"

esp_err_t temp_sensor_init(void) {
    return ESP_OK;
}

esp_err_t temp_sensor_read_c_x10(int16_t *temperature_c_x10) {
    if (temperature_c_x10 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *temperature_c_x10 = 245;
    return ESP_OK;
}
