#include "board.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "esp_log.h"


static const char *TAG = "board";

static void board_log_config(void) {
    ESP_LOGI(
        TAG,
        "CubeSat IDs: node=%d ground=%d session=0x%08lX",
        CUBESAT_NODE_ID,
        CUBESAT_GROUND_ID,
        (unsigned long)CUBESAT_DEMO_SESSION_ID
    );
    ESP_LOGI(
        TAG,
        "SX1262 pins: MOSI=%d MISO=%d SCLK=%d CS=%d RESET=%d BUSY=%d DIO1=%d",
        PIN_LORA_MOSI,
        PIN_LORA_MISO,
        PIN_LORA_SCLK,
        PIN_LORA_CS,
        PIN_LORA_RESET,
        PIN_LORA_BUSY,
        PIN_LORA_DIO1
    );
    ESP_LOGI(
        TAG,
        "GNSS pins: TX=%d RX=%d baud=%d; LoRa frequency=%lu Hz",
        PIN_GNSS_TX,
        PIN_GNSS_RX,
        CUBESAT_GNSS_BAUDRATE,
        (unsigned long)CUBESAT_LORA_FREQUENCY_HZ
    );
}

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
    board_log_config();
}
