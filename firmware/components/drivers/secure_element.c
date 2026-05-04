#include "secure_element.h"

#include "esp_random.h"

esp_err_t secure_element_init(void) {
    return ESP_OK;
}

esp_err_t secure_element_random(uint8_t *out, size_t len) {
    if (out == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_fill_random(out, len);
    return ESP_OK;
}
