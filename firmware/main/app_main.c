// Initialize the ENDURANCE firmware and start the main application tasks.

#include "esp_log.h"

#include "board.h"
#include "lora_task.h"

void system_init(void);

static const char *TAG = "app_main";

void app_main(void) {
    ESP_LOGI(TAG, "Booting ENDURANCE firmware");

    system_init();
    board_init();

    lora_task_start();

    ESP_LOGI(TAG, "Startup complete");
}