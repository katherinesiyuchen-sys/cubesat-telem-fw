#include "hardware_bringup.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "self_test.h"

#define BRINGUP_INTERVAL_MS 5000

static const char *TAG = "bringup";

void hardware_bringup_run(uint32_t boot_count) {
    ESP_LOGW(TAG, "Hardware bring-up mode enabled; normal telemetry tasks will not start");

    while (1) {
        diagnostic_report_t report;
        esp_err_t err = self_test_run(&report, boot_count);
        if (err == ESP_OK) {
            self_test_log_report(&report);
            esp_err_t tx_err = self_test_emit_report_packet(&report);
            if (tx_err == ESP_OK) {
                ESP_LOGI(TAG, "Bring-up diagnostic packet transmitted");
            } else {
                ESP_LOGW(TAG, "Bring-up diagnostic packet local-only: %s", esp_err_to_name(tx_err));
            }
        } else {
            ESP_LOGE(TAG, "Bring-up diagnostic failed to run: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(BRINGUP_INTERVAL_MS));
    }
}
