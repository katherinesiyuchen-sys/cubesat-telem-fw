// Initialize the CUBESAT firmware and start the main application tasks.

#include "esp_err.h"
#include "esp_log.h"

#include "app_state.h"
#include "board.h"
#include "config_store.h"
#include "health_task.h"
#include "logger_task.h"
#include "log_store.h"
#include "lora_task.h"
#include "sensor_task.h"

void system_init(void);

static const char *TAG = "app_main";

void app_main(void) {
    ESP_LOGI(TAG, "Booting CubeSat firmware");

    system_init();
    log_store_init();
    ESP_ERROR_CHECK(app_state_init());

    uint32_t boot_count = 0;
    esp_err_t boot_result = config_store_increment_boot_count(&boot_count);
    if (boot_result == ESP_OK) {
        ESP_LOGI(TAG, "Boot count %lu", (unsigned long)boot_count);
    } else {
        ESP_LOGW(TAG, "Boot count update failed: %s", esp_err_to_name(boot_result));
    }

    board_init();
    logger_task_start();
    health_task_start();
    sensor_task_start();

    lora_task_start();

    ESP_LOGI(TAG, "Startup complete");
}
