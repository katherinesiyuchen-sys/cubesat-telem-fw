#include "gnss_task.h"

#include "esp_log.h"

static const char *TAG = "gnss_task";

void gnss_task_start(void) {
    ESP_LOGI(TAG, "GNSS task not started separately; lora_task owns GNSS reads for demo v1");
}
