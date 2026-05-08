#include "imu.h"

#include <stdbool.h>
#include <string.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "imu";
static const TickType_t IMU_RESET_HOLD_TICKS = pdMS_TO_TICKS(10);
static const TickType_t IMU_BOOT_WAIT_TICKS = pdMS_TO_TICKS(50);
static bool s_imu_ready = false;

static esp_err_t imu_configure_pin(gpio_num_t pin, gpio_mode_t mode, int pullup, int initial_level) {
    gpio_mode_t effective_mode = mode;
    if (mode == GPIO_MODE_OUTPUT) {
        effective_mode = GPIO_MODE_INPUT_OUTPUT;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = effective_mode,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    if (mode == GPIO_MODE_OUTPUT) {
        err = gpio_set_level(pin, initial_level);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t imu_init(void) {
    esp_err_t err = imu_configure_pin((gpio_num_t)PIN_IMU_CS, GPIO_MODE_OUTPUT, 1, 1);
    if (err != ESP_OK) {
        return err;
    }

    err = imu_configure_pin((gpio_num_t)PIN_IMU_RESET, GPIO_MODE_OUTPUT, 0, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = imu_configure_pin((gpio_num_t)PIN_IMU_WAKE, GPIO_MODE_OUTPUT, 0, 1);
    if (err != ESP_OK) {
        return err;
    }
    err = imu_configure_pin((gpio_num_t)PIN_IMU_INTERRUPT, GPIO_MODE_INPUT, 1, 0);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level((gpio_num_t)PIN_IMU_RESET, 0);
    vTaskDelay(IMU_RESET_HOLD_TICKS);
    gpio_set_level((gpio_num_t)PIN_IMU_RESET, 1);
    vTaskDelay(IMU_BOOT_WAIT_TICKS);

    s_imu_ready = false;
    ESP_LOGI(
        TAG,
        "BNO085 control pins int=%d reset=%d wake=%d cs=%d ready=%d",
        gpio_get_level((gpio_num_t)PIN_IMU_INTERRUPT),
        gpio_get_level((gpio_num_t)PIN_IMU_RESET),
        gpio_get_level((gpio_num_t)PIN_IMU_WAKE),
        gpio_get_level((gpio_num_t)PIN_IMU_CS),
        s_imu_ready ? 1 : 0
    );

    return ESP_OK;
}

esp_err_t imu_read(imu_sample_t *sample) {
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));
    if (!s_imu_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_ERR_NOT_SUPPORTED;
}
