// Initialize the CUBESAT firmware and start the main application tasks.

#include "esp_err.h"
#include "esp_log.h"

#include "app_state.h"
#include "board.h"
#include "board_config.h"
#include "config_store.h"
#include "counter_store.h"
#include "hardware_bringup.h"
#include "health_task.h"
#include "logger_task.h"
#include "log_store.h"
#include "lora_task.h"
#include "sensor_task.h"
#include "self_test.h"
#include "session.h"

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
    counter_store_record_t tx_counter;
    if (counter_store_load_tx(&tx_counter) == ESP_OK && tx_counter.session_id == CUBESAT_DEMO_SESSION_ID) {
        session_init_with_counter(CUBESAT_DEMO_SESSION_ID, tx_counter.counter);
        ESP_LOGI(TAG, "Restored TX counter session=0x%08lX counter=%lu",
            (unsigned long)tx_counter.session_id,
            (unsigned long)tx_counter.counter
        );
    } else {
        session_init(CUBESAT_DEMO_SESSION_ID);
    }

#if defined(CONFIG_CUBESAT_BRINGUP_ONLY)
    hardware_bringup_run(boot_count);
    return;
#endif

    diagnostic_report_t self_test_report;
    esp_err_t self_test_result = self_test_run(&self_test_report, boot_count);
    if (self_test_result == ESP_OK) {
        self_test_log_report(&self_test_report);
        esp_err_t report_result = self_test_emit_report_packet(&self_test_report);
        if (report_result == ESP_OK) {
            ESP_LOGI(TAG, "Self-test diagnostic packet transmitted");
        } else {
            ESP_LOGW(TAG, "Self-test diagnostic packet local-only: %s", esp_err_to_name(report_result));
        }
    } else {
        ESP_LOGW(TAG, "Self-test failed to run: %s", esp_err_to_name(self_test_result));
    }

    logger_task_start();
    health_task_start();
    sensor_task_start();

    lora_task_start();

    ESP_LOGI(TAG, "Startup complete");
}
