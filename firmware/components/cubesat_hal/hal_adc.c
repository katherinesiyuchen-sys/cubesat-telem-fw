#include "hal_adc.h"

esp_err_t hal_adc_read_millivolts(int channel, int *millivolts) {
    (void)channel;
    if (millivolts == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *millivolts = 0;
    return ESP_ERR_NOT_SUPPORTED;
}
