#include "hal_gpio.h"

esp_err_t hal_gpio_config_output(gpio_num_t pin, bool initial_high) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    return gpio_set_level(pin, initial_high ? 1 : 0);
}

esp_err_t hal_gpio_config_input(gpio_num_t pin, bool pullup, bool pulldown) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&cfg);
}

esp_err_t hal_gpio_write(gpio_num_t pin, bool high) {
    return gpio_set_level(pin, high ? 1 : 0);
}

esp_err_t hal_gpio_read(gpio_num_t pin, bool *high) {
    if (high == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *high = gpio_get_level(pin) != 0;
    return ESP_OK;
}

esp_err_t hal_gpio_toggle(gpio_num_t pin) {
    int current = gpio_get_level(pin);
    return gpio_set_level(pin, current == 0 ? 1 : 0);
}
