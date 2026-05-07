#include "lora_task.h"

#include "adaptive_cadence.h"
#include "alert_logic.h"
#include "app_state.h"
#include "board_config.h"
#include "command_protocol.h"
#include "config_store.h"
#include "counter_store.h"
#include "diagnostic_protocol.h"
#include "gnss.h"
#include "lattice_protocol.h"
#include "lattice_security.h"
#include "loraq.h"
#include "packet_codec.h"
#include "replay.h"
#include "self_test.h"
#include "telemetry_protocol.h"
#include "session.h"
#include "wifi_udp_transport.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
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
#define WIFI_CONNECT_TIMEOUT_MS 10000

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

#ifndef CUBESAT_WIFI_SSID
#define CUBESAT_WIFI_SSID ""
#endif

#ifndef CUBESAT_WIFI_PASSWORD
#define CUBESAT_WIFI_PASSWORD ""
#endif

#ifndef CUBESAT_WIFI_GROUND_HOST
#define CUBESAT_WIFI_GROUND_HOST "192.168.1.100"
#endif

#ifndef CUBESAT_WIFI_LOCAL_UDP_PORT
#define CUBESAT_WIFI_LOCAL_UDP_PORT 5010
#endif

#ifndef CUBESAT_WIFI_GROUND_UDP_PORT
#define CUBESAT_WIFI_GROUND_UDP_PORT 5011
#endif

#ifndef CONFIG_CUBESAT_TRANSPORT_AUTO_FAILS
#define CONFIG_CUBESAT_TRANSPORT_AUTO_FAILS 3
#endif

#ifndef LORA_TASK_LOG_PACKET_HEX
#if defined(CONFIG_CUBESAT_LOG_PACKET_HEX)
#define LORA_TASK_LOG_PACKET_HEX 1
#else
#define LORA_TASK_LOG_PACKET_HEX 0
#endif
#endif

typedef enum {
    PACKET_TRANSPORT_LORA = 0,
    PACKET_TRANSPORT_WIFI_UDP,
    PACKET_TRANSPORT_AUTO,
    PACKET_TRANSPORT_BLE_RESERVED,
} packet_transport_mode_t;

static const char *TAG = "lora_task";
static uint32_t s_command_history[COMMAND_HISTORY_SLOTS];
static lattice_reassembly_t s_lattice_rx;
static uint8_t s_lattice_mlkem_public_key[MLKEM512_PUBLIC_KEY_LEN];
static uint8_t s_lattice_mldsa_public_key[MLDSA44_PUBLIC_KEY_LEN];
static uint8_t s_lattice_mldsa_signature[MLDSA44_SIGNATURE_LEN];
static uint8_t s_ground_mldsa_public_key[MLDSA44_PUBLIC_KEY_LEN];
static uint8_t s_ground_mlkem_ciphertext[MLKEM512_CIPHERTEXT_LEN];
static uint16_t s_ground_mlkem_transfer_id;
static bool s_ground_mldsa_public_key_valid;
static bool s_ground_mlkem_ciphertext_valid;
static bool s_lora_ready;
static bool s_wifi_ready;
static uint8_t s_lora_failures;
static packet_transport_mode_t s_configured_transport;
static packet_transport_mode_t s_active_transport;

static const char *packet_transport_name(packet_transport_mode_t mode) {
    switch (mode) {
        case PACKET_TRANSPORT_LORA:
            return "lora";
        case PACKET_TRANSPORT_WIFI_UDP:
            return "wifi";
        case PACKET_TRANSPORT_AUTO:
            return "auto";
        case PACKET_TRANSPORT_BLE_RESERVED:
            return "ble";
        default:
            return "unknown";
    }
}

static packet_transport_mode_t packet_transport_default_mode(void) {
    cubesat_app_state_t state = app_state_snapshot();
    if (state.config.transport_mode <= (uint8_t)PACKET_TRANSPORT_BLE_RESERVED) {
        return (packet_transport_mode_t)state.config.transport_mode;
    }
#if defined(CONFIG_CUBESAT_TRANSPORT_MODE_WIFI_UDP)
    return PACKET_TRANSPORT_WIFI_UDP;
#elif defined(CONFIG_CUBESAT_TRANSPORT_MODE_AUTO)
    return PACKET_TRANSPORT_AUTO;
#elif defined(CONFIG_CUBESAT_TRANSPORT_MODE_BLE_RESERVED)
    return PACKET_TRANSPORT_BLE_RESERVED;
#else
    return PACKET_TRANSPORT_LORA;
#endif
}

static bool command_arg_equals(const command_request_t *request, const char *expected) {
    if (request == NULL || expected == NULL) {
        return false;
    }

    size_t expected_len = strlen(expected);
    if (request->arg_len != expected_len) {
        return false;
    }

    for (size_t i = 0; i < expected_len; ++i) {
        char lhs = (char)tolower((unsigned char)request->arg[i]);
        char rhs = (char)tolower((unsigned char)expected[i]);
        if (lhs != rhs) {
            return false;
        }
    }
    return true;
}

static esp_err_t command_arg_to_transport(const command_request_t *request, packet_transport_mode_t *out_mode) {
    if (request == NULL || out_mode == NULL || request->arg_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (command_arg_equals(request, "lora")) {
        *out_mode = PACKET_TRANSPORT_LORA;
        return ESP_OK;
    }
    if (command_arg_equals(request, "wifi") || command_arg_equals(request, "wifi-udp")) {
        *out_mode = PACKET_TRANSPORT_WIFI_UDP;
        return ESP_OK;
    }
    if (command_arg_equals(request, "auto")) {
        *out_mode = PACKET_TRANSPORT_AUTO;
        return ESP_OK;
    }
    if (command_arg_equals(request, "ble") || command_arg_equals(request, "bluetooth")) {
        *out_mode = PACKET_TRANSPORT_BLE_RESERVED;
        return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t command_arg_to_cadence(const command_request_t *request, cubesat_cadence_mode_t *out_mode) {
    if (request == NULL || out_mode == NULL || request->arg_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (command_arg_equals(request, "auto")) {
        *out_mode = CUBESAT_CADENCE_MODE_AUTO;
        return ESP_OK;
    }
    if (command_arg_equals(request, "fast")) {
        *out_mode = CUBESAT_CADENCE_MODE_FAST;
        return ESP_OK;
    }
    if (command_arg_equals(request, "normal")) {
        *out_mode = CUBESAT_CADENCE_MODE_NORMAL;
        return ESP_OK;
    }
    if (command_arg_equals(request, "slow")) {
        *out_mode = CUBESAT_CADENCE_MODE_SLOW;
        return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static void persist_runtime_modes(packet_transport_mode_t transport_mode, cubesat_cadence_mode_t cadence_mode) {
    cubesat_runtime_config_t config;
    if (config_store_load(&config) != ESP_OK) {
        config_store_defaults(&config);
    }
    config.transport_mode = (uint8_t)transport_mode;
    config.cadence_mode = (uint8_t)cadence_mode;
    esp_err_t save_result = config_store_save(&config);
    if (save_result != ESP_OK) {
        ESP_LOGW(TAG, "Runtime config save failed: %s", esp_err_to_name(save_result));
    }
}

static esp_err_t packet_transport_can_use(packet_transport_mode_t mode) {
    switch (mode) {
        case PACKET_TRANSPORT_LORA:
            return s_lora_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
        case PACKET_TRANSPORT_WIFI_UDP:
            return s_wifi_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
        case PACKET_TRANSPORT_AUTO:
            return (s_lora_ready || s_wifi_ready) ? ESP_OK : ESP_ERR_INVALID_STATE;
        case PACKET_TRANSPORT_BLE_RESERVED:
            return ESP_ERR_NOT_SUPPORTED;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t packet_transport_set_mode(packet_transport_mode_t mode) {
    ESP_RETURN_ON_ERROR(packet_transport_can_use(mode), TAG, "transport unavailable");
    s_configured_transport = mode;
    if (mode == PACKET_TRANSPORT_AUTO) {
        s_active_transport = s_lora_ready ? PACKET_TRANSPORT_LORA : PACKET_TRANSPORT_WIFI_UDP;
    } else {
        s_active_transport = mode;
    }
    ESP_LOGW(TAG, "Packet transport set configured=%s active=%s",
        packet_transport_name(s_configured_transport),
        packet_transport_name(s_active_transport)
    );
    return ESP_OK;
}

static esp_err_t packet_transport_send(const uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (s_active_transport == PACKET_TRANSPORT_WIFI_UDP) {
        esp_err_t wifi_result = wifi_udp_transport_send(data, len, timeout_ms);
        if (wifi_result == ESP_OK || s_configured_transport != PACKET_TRANSPORT_AUTO || !s_lora_ready) {
            return wifi_result;
        }
        s_active_transport = PACKET_TRANSPORT_LORA;
        return lora_send(data, len, timeout_ms);
    }

    esp_err_t lora_result = lora_send(data, len, timeout_ms);
    if (lora_result == ESP_OK) {
        s_lora_failures = 0;
        return ESP_OK;
    }

    if (s_configured_transport == PACKET_TRANSPORT_AUTO && s_wifi_ready) {
        s_lora_failures++;
        if (s_lora_failures >= CONFIG_CUBESAT_TRANSPORT_AUTO_FAILS) {
            ESP_LOGW(TAG, "LoRa TX failed %u times; falling back to Wi-Fi UDP", (unsigned)s_lora_failures);
            s_active_transport = PACKET_TRANSPORT_WIFI_UDP;
        }
        return wifi_udp_transport_send(data, len, timeout_ms);
    }

    return lora_result;
}

static esp_err_t packet_transport_receive(uint8_t *data, size_t capacity, size_t *out_len, uint32_t timeout_ms) {
    if (s_configured_transport == PACKET_TRANSPORT_WIFI_UDP) {
        return wifi_udp_transport_receive(data, capacity, out_len, timeout_ms);
    }

    if (s_configured_transport == PACKET_TRANSPORT_AUTO) {
        if (s_lora_ready) {
            esp_err_t lora_result = lora_receive(data, capacity, out_len, timeout_ms / 2U);
            if (lora_result != ESP_ERR_TIMEOUT) {
                s_active_transport = PACKET_TRANSPORT_LORA;
                return lora_result;
            }
        }
        if (s_wifi_ready) {
            esp_err_t wifi_result = wifi_udp_transport_receive(data, capacity, out_len, timeout_ms / 2U);
            if (wifi_result != ESP_ERR_TIMEOUT) {
                s_active_transport = PACKET_TRANSPORT_WIFI_UDP;
                return wifi_result;
            }
        }
        return ESP_ERR_TIMEOUT;
    }

    return lora_receive(data, capacity, out_len, timeout_ms);
}

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
    fix->altitude_m_x10 = 110;
    fix->hdop_x100 = 90;
    fix->speed_mps_x100 = 0;
    fix->course_deg_x100 = 0;
    fix->fix_age_ms = 0;
    fix->utc_time_ms = 45319000;
    fix->utc_date_ddmmyy = 10626;
    fix->fix_type = 3;
    fix->satellites = 8;
    fix->source_flags = GNSS_FIX_FLAG_GGA | GNSS_FIX_FLAG_RMC | GNSS_FIX_FLAG_CHECKSUM;
}

static uint8_t clamp_percent_i32(int32_t value) {
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return (uint8_t)value;
}

static uint8_t estimate_link_margin_percent(const gnss_fix_t *fix, bool bench_fix) {
    if (fix == NULL || !fix->valid) {
        return 25;
    }
    int32_t margin = 62 + ((int32_t)fix->satellites * 3) - ((int32_t)fix->hdop_x100 / 12);
    if (fix->fix_age_ms > 1500U) {
        margin -= (int32_t)((fix->fix_age_ms - 1500U) / 160U);
    }
    if (bench_fix) {
        margin -= 8;
    }
    return clamp_percent_i32(margin);
}

static cubesat_cadence_input_t cadence_input_from_fix(const gnss_fix_t *fix, bool bench_fix) {
    uint8_t link_margin = estimate_link_margin_percent(fix, bench_fix);
    uint8_t risk = 12;
    if (fix == NULL || !fix->valid || fix->fix_type == 0) {
        risk += 28;
    }
    if (fix != NULL && fix->fix_age_ms > 1500U) {
        risk += 18;
    }
    if (bench_fix) {
        risk += 14;
    }
    if (link_margin < 55U) {
        risk += (uint8_t)((55U - link_margin) / 2U);
    }

    cubesat_alert_input_t alert_input = {
        .temperature_c_x10 = 230,
        .satellites = fix != NULL ? fix->satellites : 0,
        .fix_type = fix != NULL ? fix->fix_type : 0,
        .link_margin_db_x10 = (int16_t)(((int16_t)link_margin - 50) * 10),
        .replay_rejected = false,
    };
    cubesat_alert_result_t alert = alert_logic_evaluate(&alert_input);

    return (cubesat_cadence_input_t){
        .link_margin_percent = link_margin,
        .battery_percent = 92,
        .risk_percent = clamp_percent_i32(risk),
        .temperature_c_x10 = 230,
        .satellites = fix != NULL ? fix->satellites : 0,
        .hdop_x100 = fix != NULL ? fix->hdop_x100 : 999,
        .fix_age_ms = fix != NULL ? fix->fix_age_ms : 0xFFFFFFFFUL,
        .alert_active = alert.level >= CUBESAT_ALERT_WARN,
        .bench_fix = bench_fix,
    };
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
    esp_err_t tx_result = packet_transport_send(encoded, (size_t)encoded_len, LORA_TX_TIMEOUT_MS);
    if (tx_result == ESP_OK) {
        (void)counter_store_save_tx(ack_packet.session_id, ack_packet.counter);
    }
    return tx_result;
}

static esp_err_t send_lattice_object(
    uint8_t message_type,
    uint16_t transfer_id,
    const uint8_t *object,
    size_t object_len,
    uint16_t dst_id
) {
    if (object == NULL || object_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t fragments = lattice_protocol_fragment_count(object_len);
    if (fragments == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (uint16_t index = 0; index < fragments; ++index) {
        hope_packet_t packet;
        esp_err_t build_result = lattice_protocol_build_fragment_packet(
            message_type,
            transfer_id,
            index,
            object,
            object_len,
            CUBESAT_NODE_ID,
            dst_id,
            session_get_id(),
            session_next_counter(),
            (uint32_t)(esp_timer_get_time() / 1000000ULL),
            &packet
        );
        if (build_result != ESP_OK) {
            return build_result;
        }

        uint8_t encoded[HOPE_MAX_PACKET_LEN];
        int encoded_len = packet_encode(&packet, encoded, sizeof(encoded));
        if (encoded_len <= 0) {
            return ESP_FAIL;
        }

        log_packet_hex(encoded, (size_t)encoded_len);
        esp_err_t tx_result = packet_transport_send(encoded, (size_t)encoded_len, LORA_TX_TIMEOUT_MS);
        if (tx_result != ESP_OK) {
            return tx_result;
        }
        (void)counter_store_save_tx(packet.session_id, packet.counter);
    }

    ESP_LOGI(TAG, "TX lattice %s fragments=%u transfer=%u",
        lattice_protocol_message_name(message_type),
        (unsigned)fragments,
        (unsigned)transfer_id
    );
    return ESP_OK;
}

static esp_err_t send_lattice_identity(uint16_t dst_id, uint16_t transfer_id) {
    esp_err_t key_result = lattice_security_get_node_public_keys(
        s_lattice_mlkem_public_key,
        sizeof(s_lattice_mlkem_public_key),
        s_lattice_mldsa_public_key,
        sizeof(s_lattice_mldsa_public_key)
    );
    if (key_result != ESP_OK) {
        ESP_LOGW(TAG, "Lattice identity unavailable: %s", esp_err_to_name(key_result));
        return key_result;
    }

    ESP_RETURN_ON_ERROR(
        send_lattice_object(
            LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY,
            transfer_id,
            s_lattice_mlkem_public_key,
            sizeof(s_lattice_mlkem_public_key),
            dst_id
        ),
        TAG,
        "ML-KEM public key TX failed"
    );

    ESP_RETURN_ON_ERROR(
        send_lattice_object(
            LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY,
            transfer_id,
            s_lattice_mldsa_public_key,
            sizeof(s_lattice_mldsa_public_key),
            dst_id
        ),
        TAG,
        "ML-DSA public key TX failed"
    );

    ESP_RETURN_ON_ERROR(
        lattice_security_sign_handshake(
            LATTICE_SECURITY_TRANSCRIPT_ROLE_NODE_IDENTITY,
            transfer_id,
            session_get_id(),
            s_lattice_mlkem_public_key,
            sizeof(s_lattice_mlkem_public_key),
            s_lattice_mldsa_public_key,
            sizeof(s_lattice_mldsa_public_key),
            s_lattice_mldsa_signature,
            sizeof(s_lattice_mldsa_signature)
        ),
        TAG,
        "node handshake signature failed"
    );

    return send_lattice_object(
        LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE,
        transfer_id,
        s_lattice_mldsa_signature,
        sizeof(s_lattice_mldsa_signature),
        dst_id
    );
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

    esp_err_t tx_result = packet_transport_send(encoded, (size_t)encoded_len, LORA_TX_TIMEOUT_MS);
    if (tx_result != ESP_OK) {
        return tx_result;
    }

    (void)counter_store_save_tx(pkt.session_id, pkt.counter);
    app_state_record_tx(pkt.counter);
    ESP_LOGI(
        TAG,
        "TX telemetry: counter=%lu lat_e7=%ld lon_e7=%ld sats=%u hdop=%.2f age_ms=%lu bytes=%d source=%s",
        (unsigned long)pkt.counter,
        (long)fix->latitude_e7,
        (long)fix->longitude_e7,
        (unsigned)fix->satellites,
        (double)fix->hdop_x100 / 100.0,
        (unsigned long)fix->fix_age_ms,
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

static esp_err_t handle_lattice_object(uint8_t message_type, uint16_t transfer_id, const uint8_t *data, size_t len) {
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    switch ((lattice_message_type_t)message_type) {
        case LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT: {
            uint32_t new_session_id = 0;
            esp_err_t err = lattice_security_accept_mlkem_ciphertext(data, len, &new_session_id);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "ML-KEM ciphertext rejected: %s", esp_err_to_name(err));
                return err;
            }

            session_init(new_session_id);
            replay_init();
            (void)counter_store_save_tx(new_session_id, 0);
            (void)counter_store_save_rx(new_session_id, 0);
            if (len == sizeof(s_ground_mlkem_ciphertext)) {
                memcpy(s_ground_mlkem_ciphertext, data, len);
                s_ground_mlkem_transfer_id = transfer_id;
                s_ground_mlkem_ciphertext_valid = true;
            }
            ESP_LOGI(TAG, "ML-KEM session established session=0x%08lX", (unsigned long)new_session_id);
            return ESP_OK;
        }
        case LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY:
            if (len != sizeof(s_ground_mldsa_public_key)) {
                ESP_LOGW(TAG, "Ground ML-DSA public key invalid len=%u", (unsigned)len);
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(s_ground_mldsa_public_key, data, len);
            s_ground_mldsa_public_key_valid = true;
            ESP_LOGI(TAG, "Ground ML-DSA public key received len=%u", (unsigned)len);
            return ESP_OK;
        case LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE: {
            if (len != MLDSA44_SIGNATURE_LEN) {
                return ESP_ERR_INVALID_SIZE;
            }
            if (!s_ground_mldsa_public_key_valid || !s_ground_mlkem_ciphertext_valid) {
                ESP_LOGW(TAG, "Ground signature received before public key/ciphertext");
                return ESP_ERR_INVALID_STATE;
            }
            if (transfer_id != s_ground_mlkem_transfer_id) {
                ESP_LOGW(TAG, "Ground signature transfer mismatch sig=%u kem=%u",
                    (unsigned)transfer_id,
                    (unsigned)s_ground_mlkem_transfer_id
                );
                return ESP_ERR_INVALID_STATE;
            }

            esp_err_t verify_result = lattice_security_verify_handshake(
                LATTICE_SECURITY_TRANSCRIPT_ROLE_GROUND_SESSION,
                transfer_id,
                session_get_id(),
                s_ground_mlkem_ciphertext,
                sizeof(s_ground_mlkem_ciphertext),
                NULL,
                0,
                s_ground_mldsa_public_key,
                sizeof(s_ground_mldsa_public_key),
                data,
                len
            );
            if (verify_result != ESP_OK) {
                ESP_LOGW(TAG, "Ground handshake signature rejected: %s", esp_err_to_name(verify_result));
                return verify_result;
            }

            ESP_LOGI(TAG, "Ground handshake signature verified transfer=%u", (unsigned)transfer_id);
            return ESP_OK;
        }
        case LATTICE_MSG_SESSION_CONFIRM:
            ESP_LOGI(TAG, "Lattice session confirm received len=%u", (unsigned)len);
            return ESP_OK;
        default:
            ESP_LOGI(TAG, "Lattice message %s received len=%u",
                lattice_protocol_message_name(message_type),
                (unsigned)len
            );
            return ESP_OK;
    }
}

static esp_err_t handle_handshake_packet(const hope_packet_t *packet) {
    if (packet == NULL || packet->type != HOPE_PACKET_TYPE_HANDSHAKE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!command_packet_is_for_this_node(packet)) {
        ESP_LOGD(TAG, "Ignoring handshake for dst=%u", (unsigned)packet->dst_id);
        return ESP_OK;
    }

    lattice_fragment_t fragment;
    esp_err_t parse_result = lattice_protocol_parse_fragment_payload(packet->payload, packet->payload_len, &fragment);
    if (parse_result != ESP_OK) {
        ESP_LOGW(TAG, "Handshake fragment rejected: %s", esp_err_to_name(parse_result));
        return parse_result;
    }

    bool complete = false;
    esp_err_t add_result = lattice_reassembly_add(&s_lattice_rx, &fragment, &complete);
    if (add_result != ESP_OK) {
        ESP_LOGW(TAG, "Handshake reassembly failed: %s", esp_err_to_name(add_result));
        return add_result;
    }

    ESP_LOGI(TAG, "RX lattice %s fragment=%u/%u transfer=%u",
        lattice_protocol_message_name(fragment.message_type),
        (unsigned)(fragment.fragment_index + 1U),
        (unsigned)fragment.fragment_count,
        (unsigned)fragment.transfer_id
    );

    if (!complete) {
        return ESP_OK;
    }

    const uint8_t *data = lattice_reassembly_data(&s_lattice_rx);
    size_t len = lattice_reassembly_len(&s_lattice_rx);
    uint8_t message_type = lattice_reassembly_message_type(&s_lattice_rx);
    ESP_LOGI(TAG, "RX lattice %s complete bytes=%u transfer=%u",
        lattice_protocol_message_name(message_type),
        (unsigned)len,
        (unsigned)lattice_reassembly_transfer_id(&s_lattice_rx)
    );

    uint16_t transfer_id = lattice_reassembly_transfer_id(&s_lattice_rx);
    esp_err_t result = handle_lattice_object(message_type, transfer_id, data, len);
    lattice_reassembly_reset(&s_lattice_rx);
    return result;
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

    esp_err_t auth_result = lattice_security_verify_command(packet, &request);
    if (auth_result != ESP_OK) {
        ESP_LOGW(TAG, "Command auth rejected id=%lu: %s",
            (unsigned long)request.command_id,
            esp_err_to_name(auth_result)
        );
        return send_command_ack(packet, &request, COMMAND_ACK_STATUS_REJECTED, auth_result, "auth rejected");
    }

    if (command_history_contains(request.command_id)) {
        ESP_LOGI(TAG, "Duplicate command id=%lu; sending cached-success ACK",
            (unsigned long)request.command_id
        );
        return send_command_ack(packet, &request, COMMAND_ACK_STATUS_OK, ESP_OK, "duplicate command ack");
    }

    const char *message = "accepted";
    bool run_self_test = false;
    bool advertise_lattice_identity = false;
    bool switch_transport_after_ack = false;
    packet_transport_mode_t requested_transport = s_configured_transport;
    bool switch_cadence_after_ack = false;
    cubesat_cadence_mode_t requested_cadence = adaptive_cadence_mode();

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
            advertise_lattice_identity = true;
            message = lattice_security_backend_enabled() ? "lattice identity queued" : "lattice backend unavailable";
            break;
        case COMMAND_OPCODE_OPEN_DOWNLINK:
            *telemetry_paused = false;
            *telemetry_now = true;
            message = "downlink opened";
            break;
        case COMMAND_OPCODE_ARM:
            message = "node armed";
            break;
        case COMMAND_OPCODE_SET_TRANSPORT: {
            esp_err_t arg_result = command_arg_to_transport(&request, &requested_transport);
            if (arg_result != ESP_OK) {
                return send_command_ack(packet, &request, COMMAND_ACK_STATUS_REJECTED, arg_result, "transport arg invalid");
            }
            esp_err_t transport_result = packet_transport_can_use(requested_transport);
            if (transport_result != ESP_OK) {
                return send_command_ack(packet, &request, COMMAND_ACK_STATUS_REJECTED, transport_result, "transport unavailable");
            }
            switch_transport_after_ack = true;
            message = "transport switch queued";
            break;
        }
        case COMMAND_OPCODE_SET_CADENCE: {
            esp_err_t arg_result = command_arg_to_cadence(&request, &requested_cadence);
            if (arg_result != ESP_OK) {
                return send_command_ack(packet, &request, COMMAND_ACK_STATUS_REJECTED, arg_result, "cadence arg invalid");
            }
            switch_cadence_after_ack = true;
            message = "cadence switch queued";
            break;
        }
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
        ESP_LOGI(TAG, "Command id=%lu lattice auth verified key=%u",
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

    if (switch_transport_after_ack) {
        esp_err_t switch_result = packet_transport_set_mode(requested_transport);
        if (switch_result != ESP_OK) {
            ESP_LOGW(TAG, "Transport switch failed after ACK: %s", esp_err_to_name(switch_result));
        } else {
            persist_runtime_modes(s_configured_transport, adaptive_cadence_mode());
        }
    }

    if (switch_cadence_after_ack) {
        adaptive_cadence_set_mode(requested_cadence);
        persist_runtime_modes(s_configured_transport, requested_cadence);
        ESP_LOGW(TAG, "Adaptive cadence mode set to %s", adaptive_cadence_mode_name(requested_cadence));
    }

    if (run_self_test) {
        diagnostic_report_t report;
        esp_err_t self_test_result = self_test_run(&report, 0);
        if (self_test_result == ESP_OK) {
            self_test_log_report(&report);
            hope_packet_t diagnostic_packet;
            uint8_t encoded[HOPE_MAX_PACKET_LEN];
            size_t encoded_len = 0;
            esp_err_t encode_result = self_test_encode_report_packet(
                &report,
                encoded,
                sizeof(encoded),
                &encoded_len,
                &diagnostic_packet
            );
            if (encode_result == ESP_OK) {
                log_packet_hex(encoded, encoded_len);
                esp_err_t emit_result = packet_transport_send(encoded, encoded_len, LORA_TX_TIMEOUT_MS);
                if (emit_result == ESP_OK) {
                    (void)counter_store_save_tx(diagnostic_packet.session_id, diagnostic_packet.counter);
                } else {
                    ESP_LOGW(TAG, "Self-test report TX failed: %s", esp_err_to_name(emit_result));
                }
            } else {
                ESP_LOGW(TAG, "Self-test report encode failed: %s", esp_err_to_name(encode_result));
            }
        } else {
            ESP_LOGW(TAG, "Self-test command failed: %s", esp_err_to_name(self_test_result));
        }
    }

    if (advertise_lattice_identity) {
        esp_err_t identity_result = send_lattice_identity(packet->src_id, (uint16_t)(request.command_id & 0xFFFFU));
        if (identity_result != ESP_OK) {
            ESP_LOGW(TAG, "Lattice identity TX failed: %s", esp_err_to_name(identity_result));
        }
    }

    return ESP_OK;
}

static esp_err_t handle_lora_rx_window(bool *telemetry_paused, bool *telemetry_now) {
    uint8_t rx[HOPE_MAX_PACKET_LEN];
    size_t rx_len = 0;
    esp_err_t rx_result = packet_transport_receive(rx, sizeof(rx), &rx_len, LORA_RX_POLL_MS);
    if (rx_result == ESP_ERR_TIMEOUT) {
        return ESP_OK;
    }
    if (rx_result != ESP_OK) {
        ESP_LOGW(TAG, "Packet transport RX failed: %s", esp_err_to_name(rx_result));
        return rx_result;
    }

    hope_packet_t packet;
    int decode_result = packet_decode(rx, rx_len, &packet);
    if (decode_result != 0) {
        app_state_record_parse_error();
        ESP_LOGW(TAG, "Packet transport RX packet decode failed: %d", decode_result);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (packet.type == HOPE_PACKET_TYPE_HANDSHAKE) {
        return handle_handshake_packet(&packet);
    }

    return handle_command_packet(&packet, telemetry_paused, telemetry_now);
}

static void wait_for_next_telemetry(uint32_t wait_ms, bool *telemetry_paused, bool *telemetry_now) {
    if (telemetry_paused == NULL || telemetry_now == NULL) {
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
        return;
    }

    uint32_t elapsed_ms = 0;
    while (elapsed_ms < wait_ms && !*telemetry_now) {
        (void)handle_lora_rx_window(telemetry_paused, telemetry_now);
        if (*telemetry_now) {
            break;
        }
        uint32_t slice_ms = wait_ms - elapsed_ms;
        if (slice_ms > LORA_RX_POLL_MS) {
            slice_ms = LORA_RX_POLL_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(slice_ms));
        elapsed_ms += slice_ms;
    }
}

static esp_err_t init_lora_transport(const lora_config_t *lora_config) {
    if (lora_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t lora_init_result = lora_init(lora_config);
    if (lora_init_result == ESP_OK) {
        s_lora_ready = true;
        ESP_LOGI(TAG, "LoRa transport online");
    }
    return lora_init_result;
}

static esp_err_t init_wifi_transport(void) {
    wifi_udp_transport_config_t wifi_config = {
        .ssid = CUBESAT_WIFI_SSID,
        .password = CUBESAT_WIFI_PASSWORD,
        .ground_host = CUBESAT_WIFI_GROUND_HOST,
        .local_port = CUBESAT_WIFI_LOCAL_UDP_PORT,
        .ground_port = CUBESAT_WIFI_GROUND_UDP_PORT,
        .connect_timeout_ms = WIFI_CONNECT_TIMEOUT_MS,
    };
    esp_err_t wifi_result = wifi_udp_transport_init(&wifi_config);
    if (wifi_result == ESP_OK) {
        s_wifi_ready = true;
        ESP_LOGI(TAG, "Wi-Fi UDP transport online local=%u ground=%s:%u",
            (unsigned)CUBESAT_WIFI_LOCAL_UDP_PORT,
            CUBESAT_WIFI_GROUND_HOST,
            (unsigned)CUBESAT_WIFI_GROUND_UDP_PORT
        );
    } else {
        s_wifi_ready = false;
    }
    return wifi_result;
}

static void init_packet_transports(const lora_config_t *lora_config) {
    s_configured_transport = packet_transport_default_mode();
    s_active_transport = PACKET_TRANSPORT_LORA;

    while (1) {
        if (s_configured_transport == PACKET_TRANSPORT_LORA || s_configured_transport == PACKET_TRANSPORT_AUTO) {
            esp_err_t lora_result = init_lora_transport(lora_config);
            if (lora_result != ESP_OK) {
                ESP_LOGW(TAG, "LoRa init failed: %s", esp_err_to_name(lora_result));
            }
        }

        if (s_configured_transport == PACKET_TRANSPORT_WIFI_UDP || s_configured_transport == PACKET_TRANSPORT_AUTO) {
            esp_err_t wifi_result = init_wifi_transport();
            if (wifi_result != ESP_OK) {
                ESP_LOGW(TAG, "Wi-Fi UDP init failed: %s", esp_err_to_name(wifi_result));
            }
        }

        if (s_configured_transport == PACKET_TRANSPORT_BLE_RESERVED) {
            ESP_LOGW(TAG, "BLE transport is reserved; trying LoRa, then Wi-Fi UDP");
            (void)init_lora_transport(lora_config);
            if (!s_lora_ready) {
                (void)init_wifi_transport();
            }
            if (s_lora_ready) {
                s_configured_transport = PACKET_TRANSPORT_LORA;
            } else if (s_wifi_ready) {
                s_configured_transport = PACKET_TRANSPORT_WIFI_UDP;
            }
        }

        if (packet_transport_set_mode(s_configured_transport) == ESP_OK) {
            return;
        }

        ESP_LOGW(TAG, "No packet transport available; retrying");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void lora_task(void *arg) {
    (void)arg;

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

    init_packet_transports(&lora_config);

    replay_init();
    lattice_reassembly_reset(&s_lattice_rx);
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
    cubesat_app_state_t initial_state = app_state_snapshot();
    cubesat_cadence_mode_t initial_cadence = CUBESAT_CADENCE_MODE_AUTO;
    if (initial_state.config.cadence_mode <= (uint8_t)CUBESAT_CADENCE_MODE_SLOW) {
        initial_cadence = (cubesat_cadence_mode_t)initial_state.config.cadence_mode;
    }
    adaptive_cadence_init(initial_cadence);
    cubesat_cadence_class_t last_cadence_class = CUBESAT_CADENCE_CLASS_NOMINAL;

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

        cubesat_cadence_input_t cadence_input = cadence_input_from_fix(&fix, bench_fix);
        const cubesat_cadence_state_t *cadence = adaptive_cadence_update(&cadence_input);
        if (cadence != NULL && cadence->classifier != last_cadence_class) {
            ESP_LOGW(
                TAG,
                "Adaptive cadence class=%s anomaly=%u interval=%lums target=%lums reason=%s mode=%s",
                adaptive_cadence_class_name(cadence->classifier),
                (unsigned)cadence->anomaly_score,
                (unsigned long)cadence->interval_ms,
                (unsigned long)cadence->target_interval_ms,
                cadence->reason,
                adaptive_cadence_mode_name(adaptive_cadence_mode())
            );
            last_cadence_class = cadence->classifier;
        }

        esp_err_t tx_result = transmit_telemetry_from_fix(&fix, bench_fix);
        telemetry_now = false;
        if (tx_result == ESP_ERR_INVALID_ARG || tx_result == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "GNSS fix was not usable");
            continue;
        }
        if (tx_result != ESP_OK) {
            ESP_LOGE(TAG, "Packet transport TX failed: %s", esp_err_to_name(tx_result));
            continue;
        }

        uint32_t wait_ms = cadence != NULL ? cadence->interval_ms : 2000U;
        ESP_LOGI(
            TAG,
            "Next telemetry in %lums class=%s anomaly=%u reason=%s",
            (unsigned long)wait_ms,
            cadence != NULL ? adaptive_cadence_class_name(cadence->classifier) : "UNKNOWN",
            cadence != NULL ? (unsigned)cadence->anomaly_score : 0U,
            cadence != NULL ? cadence->reason : "fallback"
        );
        wait_for_next_telemetry(wait_ms, &telemetry_paused, &telemetry_now);
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
