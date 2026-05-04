#include "logger_task.h"

#include "esp_log.h"
#include "log_store.h"

static const char *TAG = "logger_task";

void logger_task_start(void) {
    ESP_LOGI(TAG, "logger task ready recent_records=%u", (unsigned)log_store_count());
}
