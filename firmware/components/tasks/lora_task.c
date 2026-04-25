#include "lora_task.h"
#include "app_config.h"

#include "telemetry_protocol.h"
#include "session.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


//builds fake packet every 2 seconds and logs it, simulating the process of preparing a packet for transmission over LoRa.
static const char *TAG = "lora_task";

static void lora_task(void *arg) {
    (void)arg;

    session_init();

    while (1) {
        hope_packet_t pkt;

        telemetry_protocol_build(&pkt);
        pkt.counter = session_next_counter();

        ESP_LOGI(
            TAG,
            "Prepared packet: type=%u counter=%lu payload_len=%u",
            pkt.type,
            (unsigned long)pkt.counter,
            (unsigned)pkt.payload_len
        );

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void lora_task_start(void) {
    xTaskCreate(
        lora_task,
        "lora_task",
        LORA_TASK_STACK_SIZE,
        NULL,
        LORA_TASK_PRIORITY,
        NULL
    );
}