#include "lora_task.h"

#include "app_state.h"
#include "board_config.h"
#include "command_protocol.h"
#include "counter_store.h"
#include "diagnostic_protocol.h"
#include "gnss.h"
#include "loraq.h"
#include "packet_codec.h"
#include "replay.h"
#include "self_test.h"
#include "telemetry_protocol.h"
#include "session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
#define LORA_RX_POLL_MS 250
#define GNSS_FIX_TIMEOUT_MS 1000
#define COMMAND_HISTORY_SLOTS 12

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
static uint32_t s_command_history[COMMAND_HISTORY_SLOTS];

static bool command_history_contains(uint32_t command_id) {
    for (size_t i = 0; i < COMMAND_HISTORY_SLOTS; ++i) {
        if (s_command_history[i] == command_id) {
            return true;
        }
    }
    return false;
}

static void command_history_mark(uint32_t command_id) {
    if (command_id == 0 || command_history_contains(command_id)) {
        return;
    }
    for (size_t i = COMMAND_HISTORY_SLOTS - 1; i > 0; --i) {
        s_command_history[i] = s_command_history[i - 1];
    }
    s_command_history[0] = command_id;
}

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

static int16_t err_to_i16(esp_err_t err) {
    if (err > INT16_MAX) {
        return INT16_MAX;
    }
    if (err < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)err;
}

static void ack_message_set(command_ack_t *ack, const char *message) {
    if (ack == NULL || message == NULL) {
        return;
    }

    size_t len = strlen(message);
    if (len > COMMAND_ACK_MAX_MESSAGE_LEN) {
        len = COMMAND_ACK_MAX_MESSAGE_LEN;
    }
    memcpy(ack->message, message, len);
    ack->message[len] = '\0';
    ack->message_len = (uint8_t)len;
}

static esp_err_t send_command_ack(
    const hope_packet_t *request_packet,
    const command_request_t *request,
    command_ack_status_t status,
    esp_err_t detail,
    const char *message
) {
    if (request_packet == NULL || request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    command_ack_t ack = {
        .version = COMMAND_PAYLOAD_VERSION,
        .acked_type = HOPE_PACKET_TYPE_COMMAND,
        .command_id = request->command_id,
        .status = (uint8_t)status,
        .detail_code = err_to_i16(detail),
    };
    ack_message_set(&ack, message);

    hope_packet_t ack_packet;
    esp_err_t build_result = command_protocol_build_ack_packet(
        &ack,
        CUBESAT_NODE_ID,
        request_packet->src_id,
        request_packet->session_id,
        session_next_counter(),
        (uint32_t)(esp_timer_get_time() / 1000000ULL),
        &ack_packet
    );
    if (build_result != ESP_OK) {
        return build_result;
    }

    uint8_t encoded[HOPE_MAX_PACKET_LEN];
    int encoded_len = packet_encode(&ack_packet, encoded, sizeof(encoded));
    if (encoded_len <= 0) {
        return ESP_FAIL;
    }

    log_packet_hex(encoded, (size_t)encoded_len);
    esp_err_t tx_result = lora_send(encoded, (size_t)encoded_len, LORA_TX_TIMEOUT_MS);
    if (tx_result == ESP_OK) {
        (void)counter_store_save_tx(ack_packet.session_id, ack_packet.counter);
    }
    return tx_result;
}

static esp_err_t transmit_telemetry_from_fix(const gnss_fix_t *fix, bool bench_fix) {
    if (fix == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    hope_packet_t pkt;
    uint8_t encoded[HOPE_MAX_PACKET_LEN];

    esp_err_t build_result = telemetry_protocol_build_from_gnss(fix, &pkt);
    if (build_result != ESP_OK) {
        return build_result;
    }

    pkt.session_id = session_get_id();
    pkt.counter = session_next_counter();
    pkt.timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    pkt.src_id = CUBESAT_NODE_ID;
    pkt.dst_id = CUBESAT_GROUND_ID;

    int encoded_len = packet_encode(&pkt, encoded, sizeof(encoded));
    if (encoded_len <= 0) {
        ESP_LOGE(TAG, "Packet encode failed: %d", encoded_len);
        return ESP_FAIL;
    }

    log_packet_hex(encoded, (size_t)encoded_len);

    esp_err_t tx_result = lora_send(encoded, (size_t)encoded_len, LORA_TX_TIMEOUT_MS);
    if (tx_result != ESP_OK) {
        return tx_result;
    }

    (void)counter_store_save_tx(pkt.session_id, pkt.counter);
    app_state_record_tx(pkt.counter);
    ESP_LOGI(
        TAG,
        "TX telemetry: counter=%lu lat_e7=%ld lon_e7=%ld sats=%u bytes=%d source=%s",
        (unsigned long)pkt.counter,
        (long)fix->latitude_e7,
        (long)fix->longitude_e7,
        (unsigned)fix->satellites,
        encoded_len,
        bench_fix ? "bench" : "gnss"
    );

    ESP_LOGD(
        TAG,
        "Packet detail: type=%u payload_len=%u",
        pkt.type,
        (unsigned)pkt.payload_len
    );

    return ESP_OK;
}

static bool command_packet_is_for_this_node(const hope_packet_t *packet) {
    if (packet == NULL) {
        return false;
    }
    return packet->dst_id == CUBESAT_NODE_ID || packet->dst_id == 0xFFFF;
}

static esp_err_t handle_command_packet(
    const hope_packet_t *packet,
    bool *telemetry_paused,
    bool *telemetry_now
) {
    if (packet == NULL || telemetry_paused == NULL || telemetry_now == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (packet->type != HOPE_PACKET_TYPE_COMMAND) {
        return ESP_OK;
    }
    if (!command_packet_is_for_this_node(packet)) {
        ESP_LOGD(TAG, "Ignoring command for dst=%u", (unsigned)packet->dst_id);
        return ESP_OK;
    }

    if (!replay_check_session_and_update(packet->session_id, packet->counter)) {
        app_state_record_replay_reject();
        ESP_LOGW(TAG, "Rejected replay command session=0x%08lX counter=%lu",
            (unsigned long)packet->session_id,
            (unsigned long)packet->counter
        );
        return ESP_ERR_INVALID_STATE;
    }

    app_state_record_rx(packet->counter);
    (void)counter_store_save_rx(packet->session_id, packet->counter);

    command_request_t request;
    esp_err_t parse_result = command_protocol_parse_request_payload(packet->payload, packet->payload_len, &request);
    if (parse_result != ESP_OK) {
        ESP_LOGW(TAG, "Command payload rejected: %s", esp_err_to_name(parse_result));
        return parse_result;
    }

    if (command_history_contains(request.command_id)) {
        ESP_LOGI(TAG, "Duplicate command id=%lu; sending cached-success ACK",
            (unsigned long)request.command_id
        );
        return send_command_ack(packet, &request, COMMAND_ACK_STATUS_OK, ESP_OK, "duplicate command ack");
    }

    const char *message = "accepted";
    bool run_self_test = false;

    switch ((command_opcode_t)request.opcode) {
        case COMMAND_OPCODE_SELF_TEST:
            run_self_test = true;
            message = "selftest queued";
            break;
        case COMMAND_OPCODE_PING:
            *telemetry_now = true;
            message = "pong telemetry queued";
            break;
        case COMMAND_OPCODE_TELEMETRY_NOW:
            *telemetry_now = true;
            message = "telemetry queued";
            break;
        case COMMAND_OPCODE_PAUSE_TELEMETRY:
        case COMMAND_OPCODE_ISOLATE:
            *telemetry_paused = true;
            message = "telemetry paused";
            break;
        case COMMAND_OPCODE_RESUME_TELEMETRY:
        case COMMAND_OPCODE_CONNECT:
            *telemetry_paused = false;
            *telemetry_now = true;
            message = "telemetry resumed";
            break;
        case COMMAND_OPCODE_ROTATE_SESSION:
            message = "session rotation reserved";
            break;
        case COMMAND_OPCODE_OPEN_DOWNLINK:
            *telemetry_paused = false;
            *telemetry_now = true;
            message = "downlink opened";
            break;
        case COMMAND_OPCODE_ARM:
            message = "node armed";
            break;
        default:
            return send_command_ack(packet, &request, COMMAND_ACK_STATUS_REJECTED, ESP_ERR_NOT_SUPPORTED, "opcode unsupported");
    }

    ESP_LOGI(
        TAG,
        "RX command id=%lu opcode=%s session=0x%08lX counter=%lu",
        (unsigned long)request.command_id,
        command_protocol_opcode_name(request.opcode),
        (unsigned long)packet->session_id,
        (unsigned long)packet->counter
    );

    if ((request.flags & COMMAND_FLAG_AUTH_PRESENT) != 0) {
        ESP_LOGI(TAG, "Command id=%lu carries auth placeholder key=%u",
            (unsigned long)request.command_id,
            (unsigned)request.auth_key_id
        );
    }

    command_history_mark(request.command_id);
    esp_err_t ack_result = send_command_ack(packet, &request, COMMAND_ACK_STATUS_OK, ESP_OK, message);
    if (ack_result != ESP_OK) {
        ESP_LOGW(TAG, "Command ACK failed: %s", esp_err_to_name(ack_result));
        return ack_result;
    }

    if (run_self_test) {
        diagnostic_report_t report;
        esp_err_t self_test_result = self_test_run(&report, 0);
        if (self_test_result == ESP_OK) {
            self_test_log_report(&report);
            esp_err_t emit_result = self_test_emit_report_packet(&report);
            if (emit_result != ESP_OK) {
                ESP_LOGW(TAG, "Self-test report TX failed: %s", esp_err_to_name(emit_result));
            }
        } else {
            ESP_LOGW(TAG, "Self-test command failed: %s", esp_err_to_name(self_test_result));
        }
    }

    return ESP_OK;
}

static esp_err_t handle_lora_rx_window(bool *telemetry_paused, bool *telemetry_now) {
    uint8_t rx[HOPE_MAX_PACKET_LEN];
    size_t rx_len = 0;
    esp_err_t rx_result = lora_receive(rx, sizeof(rx), &rx_len, LORA_RX_POLL_MS);
    if (rx_result == ESP_ERR_TIMEOUT) {
        return ESP_OK;
    }
    if (rx_result != ESP_OK) {
        ESP_LOGW(TAG, "LoRa RX failed: %s", esp_err_to_name(rx_result));
        return rx_result;
    }

    hope_packet_t packet;
    int decode_result = packet_decode(rx, rx_len, &packet);
    if (decode_result != 0) {
        app_state_record_parse_error();
        ESP_LOGW(TAG, "LoRa RX packet decode failed: %d", decode_result);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return handle_command_packet(&packet, telemetry_paused, telemetry_now);
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
        .spreading_factor = CUBESAT_LORA_SPREADING_FACTOR,
        .bandwidth_hz = CUBESAT_LORA_BANDWIDTH_HZ,
        .coding_rate = CUBESAT_LORA_CODING_RATE,
        .tx_power_dbm = CUBESAT_LORA_TX_POWER_DBM,
    };

    esp_err_t gnss_init_result = gnss_init(&gnss_config);
    if (gnss_init_result != ESP_OK) {
        ESP_LOGW(TAG, "GNSS init failed; telemetry will wait for recovery: %s", esp_err_to_name(gnss_init_result));
    }

    esp_err_t lora_init_result = lora_init(&lora_config);
    while (lora_init_result != ESP_OK) {
        ESP_LOGW(TAG, "LoRa init failed; retrying: %s", esp_err_to_name(lora_init_result));
        vTaskDelay(pdMS_TO_TICKS(5000));
        lora_init_result = lora_init(&lora_config);
    }

    replay_init();
    counter_store_record_t rx_counter;
    if (counter_store_load_rx(&rx_counter) == ESP_OK && rx_counter.session_id == session_get_id()) {
        if (replay_restore_session_counter(rx_counter.session_id, rx_counter.counter)) {
            ESP_LOGI(TAG, "Restored RX replay counter session=0x%08lX counter=%lu",
                (unsigned long)rx_counter.session_id,
                (unsigned long)rx_counter.counter
            );
        }
    }
    bool telemetry_paused = false;
    bool telemetry_now = false;

    while (1) {
        gnss_fix_t fix;

        (void)handle_lora_rx_window(&telemetry_paused, &telemetry_now);

        if (telemetry_paused && !telemetry_now) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

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

        esp_err_t tx_result = transmit_telemetry_from_fix(&fix, bench_fix);
        telemetry_now = false;
        if (tx_result == ESP_ERR_INVALID_ARG || tx_result == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "GNSS fix was not usable");
            continue;
        }
        if (tx_result != ESP_OK) {
            ESP_LOGE(TAG, "LoRa TX failed: %s", esp_err_to_name(tx_result));
            continue;
        }

        (void)handle_lora_rx_window(&telemetry_paused, &telemetry_now);
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
