#include "imu.h"

#include <string.h>

esp_err_t imu_init(void) {
    return ESP_OK;
}

esp_err_t imu_read(imu_sample_t *sample) {
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));
    return ESP_OK;
}
