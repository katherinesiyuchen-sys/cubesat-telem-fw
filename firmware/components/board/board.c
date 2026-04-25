#include "board.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "esp_log.h"


//just turn on the status LED for now, more board initialization can be added here as needed.
static const char *TAG = "board";

void board_init(void) {
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << PIN_STATUS_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    gpio_set_level(PIN_STATUS_LED, 1);

    ESP_LOGI(TAG, "Board initialized");
}