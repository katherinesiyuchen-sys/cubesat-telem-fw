#include "health_task.h"

#include "app_state.h"
#include "esp_log.h"

static const char *TAG = "health_task";

void health_task_start(void) {
    cubesat_app_state_t snapshot = app_state_snapshot();
    ESP_LOGI(
        TAG,
        "health ready tx=%lu rx=%lu replay=%lu parse_errors=%lu",
        (unsigned long)snapshot.tx_packets,
        (unsigned long)snapshot.rx_packets,
        (unsigned long)snapshot.replay_rejects,
        (unsigned long)snapshot.parse_errors
    );
}
