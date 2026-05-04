#include "lora_task.h"

#include "board_config.h"
#include "gnss.h"
#include "loraq.h"
#include "packet_codec.h"
#include "telemetry_protocol.h"
#include "session.h"

#include <stdbool.h>
#include <stddef.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef LORA_TASK_STACK_SIZE
#define LORA_TASK_STACK_SIZE 4096
#endif

#ifndef LORA_TASK_PRIORITY
#define LORA_TASK_PRIORITY 5
#endif

#define LORA_TX_TIMEOUT_MS 5000
#define GNSS_FIX_TIMEOUT_MS 5000

#ifndef CUBESAT_NODE_ID
#define CUBESAT_NODE_ID 1
#endif

#ifndef CUBESAT_GROUND_ID
#define CUBESAT_GROUND_ID 2
#endif

#ifndef CUBESAT_DEMO_SESSION_ID
#define CUBESAT_DEMO_SESSION_ID 0x12345678UL
#endif

#ifndef CUBESAT_LORA_FREQUENCY_HZ
#define CUBESAT_LORA_FREQUENCY_HZ 915000000UL
#endif

#ifndef CUBESAT_GNSS_BAUDRATE
#define CUBESAT_GNSS_BAUDRATE 9600
#endif

#ifndef LORA_TASK_LOG_PACKET_HEX
#if defined(CONFIG_CUBESAT_LOG_PACKET_HEX)
#define LORA_TASK_LOG_PACKET_HEX 1
#else
#define LORA_TASK_LOG_PACKET_HEX 0
#endif
#endif

static const char *TAG = "lora_task";

static void make_bench_fix(gnss_fix_t *fix) {
    if (fix == NULL) {
        return;
    }

    fix->valid = true;
    fix->latitude_e7 = 378715000;
    fix->longitude_e7 = -1222730000;
    fix->fix_type = 3;
    fix->satellites = 8;
}

static void log_packet_hex(const uint8_t *data, size_t len) {
#if LORA_TASK_LOG_PACKET_HEX
    char hex[(HOPE_MAX_PACKET_LEN * 2) + 1];
    static const char digits[] = "0123456789ABCDEF";

    if (data == NULL || len > HOPE_MAX_PACKET_LEN) {
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        hex[i * 2] = digits[(data[i] >> 4) & 0x0F];
        hex[(i * 2) + 1] = digits[data[i] & 0x0F];
    }
    hex[len * 2] = '\0';
    ESP_LOGI(TAG, "TX_HEX %s", hex);
#else
    (void)data;
    (void)len;
#endif
}

static void lora_task(void *arg) {
    (void)arg;

    session_init(CUBESAT_DEMO_SESSION_ID);

    gnss_config_t gnss_config = {
        .uart_port = UART_NUM_1,
        .pin_tx = PIN_GNSS_TX,
        .pin_rx = PIN_GNSS_RX,
        .baudrate = CUBESAT_GNSS_BAUDRATE,
    };

    lora_config_t lora_config = {
        .spi_host = SPI2_HOST,
        .pin_mosi = PIN_LORA_MOSI,
        .pin_miso = PIN_LORA_MISO,
        .pin_sclk = PIN_LORA_SCLK,
        .pin_cs = PIN_LORA_CS,
        .pin_reset = PIN_LORA_RESET,
        .pin_busy = PIN_LORA_BUSY,
        .pin_dio1 = PIN_LORA_DIO1,
        .frequency_hz = CUBESAT_LORA_FREQUENCY_HZ,
        .spreading_factor = 7,
        .bandwidth_hz = 125000,
        .coding_rate = 0x01,
        .tx_power_dbm = 14,
    };

    ESP_ERROR_CHECK(gnss_init(&gnss_config));
    ESP_ERROR_CHECK(lora_init(&lora_config));

    while (1) {
        gnss_fix_t fix;
        hope_packet_t pkt;
        uint8_t encoded[HOPE_MAX_PACKET_LEN];

        bool bench_fix = false;
        esp_err_t fix_result = gnss_read_fix(&fix, GNSS_FIX_TIMEOUT_MS);
        if (fix_result != ESP_OK) {
#if defined(CONFIG_CUBESAT_FAKE_TELEMETRY_ON_GNSS_TIMEOUT)
            make_bench_fix(&fix);
            bench_fix = true;
            ESP_LOGW(TAG, "GNSS fix timeout; using bench telemetry fallback");
#else
            ESP_LOGW(TAG, "Waiting for valid GNSS fix");
            continue;
#endif
        }

        esp_err_t build_result = telemetry_protocol_build_from_gnss(&fix, &pkt);
        if (build_result != ESP_OK) {
            ESP_LOGW(TAG, "GNSS fix was not usable");
            continue;
        }

        pkt.session_id = session_get_id();
        pkt.counter = session_next_counter();
        pkt.timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        pkt.src_id = CUBESAT_NODE_ID;
        pkt.dst_id = CUBESAT_GROUND_ID;

        int encoded_len = packet_encode(&pkt, encoded, sizeof(encoded));
        if (encoded_len <= 0) {
            ESP_LOGE(TAG, "Packet encode failed: %d", encoded_len);
            continue;
        }

        log_packet_hex(encoded, (size_t)encoded_len);

        esp_err_t tx_result = lora_send(encoded, (size_t)encoded_len, LORA_TX_TIMEOUT_MS);
        if (tx_result != ESP_OK) {
            ESP_LOGE(TAG, "LoRa TX failed: %s", esp_err_to_name(tx_result));
            continue;
        }

        ESP_LOGI(
            TAG,
            "TX telemetry: counter=%lu lat_e7=%ld lon_e7=%ld sats=%u bytes=%d source=%s",
            (unsigned long)pkt.counter,
            (long)fix.latitude_e7,
            (long)fix.longitude_e7,
            (unsigned)fix.satellites,
            encoded_len,
            bench_fix ? "bench" : "gnss"
        );

        ESP_LOGD(
            TAG,
            "Packet detail: type=%u payload_len=%u",
            pkt.type,
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
