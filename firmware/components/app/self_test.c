#include "self_test.h"

#include <stdbool.h>
#include <string.h>

#include "board_config.h"
#include "config_store.h"
#include "counter_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "gnss.h"
#include "hal_i2c.h"
#include "loraq.h"
#include "packet_codec.h"
#include "secure_element.h"
#include "session.h"

#define I2C_SELF_TEST_CLOCK_HZ 100000
#define I2C_PROBE_TIMEOUT_MS 20
#define GNSS_SELF_TEST_FIX_TIMEOUT_MS 250
#define SELF_TEST_LORA_TX_TIMEOUT_MS 5000

static const char *TAG = "self_test";

static const uint8_t I2C_PROBE_ADDRESSES[] = {
    0x1E, // magnetometer class devices
    0x40, // INA/voltage monitor class devices
    0x48, // temperature/ADC class devices
    0x68, // IMU/RTC class devices
    0x76, // pressure/environment class devices
    0x77,
};

static int16_t err_to_i16(esp_err_t err) {
    if (err > INT16_MAX) {
        return INT16_MAX;
    }
    if (err < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)err;
}

static void mark_pass(diagnostic_report_t *report, uint16_t check) {
    report->passed_mask |= check;
    report->warning_mask &= (uint16_t)~check;
    report->failed_mask &= (uint16_t)~check;
}

static void mark_warn(diagnostic_report_t *report, uint16_t check) {
    report->warning_mask |= check;
    report->passed_mask &= (uint16_t)~check;
    report->failed_mask &= (uint16_t)~check;
}

static void mark_fail(diagnostic_report_t *report, uint16_t check) {
    report->failed_mask |= check;
    report->passed_mask &= (uint16_t)~check;
    report->warning_mask &= (uint16_t)~check;
}

static void finalize_status(diagnostic_report_t *report) {
    if (report->failed_mask != 0) {
        report->overall_status = DIAGNOSTIC_STATUS_FAIL;
    } else if (report->warning_mask != 0) {
        report->overall_status = DIAGNOSTIC_STATUS_WARN;
    } else {
        report->overall_status = DIAGNOSTIC_STATUS_PASS;
    }
}

static void fill_pin_map(diagnostic_report_t *report) {
    const int pins[DIAGNOSTIC_PIN_COUNT] = {
        PIN_STATUS_LED,
        PIN_LORA_MOSI,
        PIN_LORA_MISO,
        PIN_LORA_SCLK,
        PIN_LORA_CS,
        PIN_LORA_RESET,
        PIN_LORA_BUSY,
        PIN_LORA_DIO1,
        PIN_GNSS_TX,
        PIN_GNSS_RX,
        PIN_I2C_SDA,
        PIN_I2C_SCL,
    };

    bool valid = true;
    for (size_t i = 0; i < DIAGNOSTIC_PIN_COUNT; ++i) {
        if (pins[i] < 0 || pins[i] > 255) {
            report->pins[i] = 0xFF;
            valid = false;
            continue;
        }

        report->pins[i] = (uint8_t)pins[i];
        for (size_t j = 0; j < i; ++j) {
            if (report->pins[j] == report->pins[i]) {
                valid = false;
            }
        }
    }

    if (valid) {
        mark_pass(report, DIAGNOSTIC_CHECK_PIN_MAP);
    } else {
        mark_fail(report, DIAGNOSTIC_CHECK_PIN_MAP);
    }
}

static void run_i2c_check(diagnostic_report_t *report) {
    hal_i2c_config_t config = {
        .port = HAL_I2C_PORT_0,
        .sda_pin = PIN_I2C_SDA,
        .scl_pin = PIN_I2C_SCL,
        .clock_hz = I2C_SELF_TEST_CLOCK_HZ,
    };

    esp_err_t err = hal_i2c_master_init(&config);
    report->i2c_status = err_to_i16(err);
    if (err != ESP_OK) {
        mark_fail(report, DIAGNOSTIC_CHECK_I2C_BUS);
        return;
    }

    uint8_t seen = 0;
    for (size_t i = 0; i < sizeof(I2C_PROBE_ADDRESSES); ++i) {
        if (hal_i2c_probe(I2C_PROBE_ADDRESSES[i], I2C_PROBE_TIMEOUT_MS) == ESP_OK) {
            seen++;
        }
    }

    report->i2c_devices_seen = seen;
    if (seen == 0) {
        report->i2c_status = err_to_i16(ESP_ERR_NOT_FOUND);
        mark_warn(report, DIAGNOSTIC_CHECK_I2C_BUS);
    } else {
        report->i2c_status = err_to_i16(ESP_OK);
        mark_pass(report, DIAGNOSTIC_CHECK_I2C_BUS);
    }

    (void)hal_i2c_bus_reset();
}

static void run_gnss_check(diagnostic_report_t *report) {
    gnss_config_t config = {
        .uart_port = UART_NUM_1,
        .pin_tx = PIN_GNSS_TX,
        .pin_rx = PIN_GNSS_RX,
        .baudrate = CUBESAT_GNSS_BAUDRATE,
    };

    esp_err_t err = gnss_init(&config);
    report->gnss_status = err_to_i16(err);
    if (err != ESP_OK) {
        mark_fail(report, DIAGNOSTIC_CHECK_GNSS_UART);
        return;
    }

    gnss_fix_t fix = { 0 };
    err = gnss_read_fix(&fix, GNSS_SELF_TEST_FIX_TIMEOUT_MS);
    report->gnss_status = err_to_i16(err);
    if (err == ESP_OK && fix.valid) {
        mark_pass(report, DIAGNOSTIC_CHECK_GNSS_UART);
    } else {
        mark_warn(report, DIAGNOSTIC_CHECK_GNSS_UART);
    }
}

static void run_lora_check(diagnostic_report_t *report) {
    lora_config_t config = {
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

    esp_err_t err = lora_init(&config);
    report->lora_status = err_to_i16(err);
    if (err == ESP_OK) {
        mark_pass(report, DIAGNOSTIC_CHECK_LORA_RADIO);
    } else {
        mark_fail(report, DIAGNOSTIC_CHECK_LORA_RADIO);
    }
}

static void run_rng_check(diagnostic_report_t *report) {
    uint8_t random_bytes[16] = { 0 };
    esp_err_t err = secure_element_init();
    if (err == ESP_OK) {
        err = secure_element_random(random_bytes, sizeof(random_bytes));
    }

    report->rng_status = err_to_i16(err);
    if (err != ESP_OK) {
        mark_fail(report, DIAGNOSTIC_CHECK_SECURE_RNG);
        return;
    }

    bool any_nonzero = false;
    for (size_t i = 0; i < sizeof(random_bytes); ++i) {
        any_nonzero = any_nonzero || random_bytes[i] != 0;
    }

    if (any_nonzero) {
        mark_pass(report, DIAGNOSTIC_CHECK_SECURE_RNG);
    } else {
        report->rng_status = err_to_i16(ESP_FAIL);
        mark_fail(report, DIAGNOSTIC_CHECK_SECURE_RNG);
    }
}

static void run_nvs_check(diagnostic_report_t *report) {
    cubesat_runtime_config_t config;
    esp_err_t err = config_store_load(&config);
    report->nvs_status = err_to_i16(err);
    if (err == ESP_OK) {
        mark_pass(report, DIAGNOSTIC_CHECK_NVS_CONFIG);
    } else {
        mark_fail(report, DIAGNOSTIC_CHECK_NVS_CONFIG);
    }
}

static const char *status_name(uint8_t status) {
    switch (status) {
        case DIAGNOSTIC_STATUS_PASS:
            return "PASS";
        case DIAGNOSTIC_STATUS_WARN:
            return "WARN";
        case DIAGNOSTIC_STATUS_FAIL:
            return "FAIL";
        default:
            return "UNKNOWN";
    }
}

static void log_packet_hex(const uint8_t *data, size_t len) {
    static const char digits[] = "0123456789ABCDEF";
    char hex[(HOPE_MAX_PACKET_LEN * 2) + 1];

    if (data == NULL || len > HOPE_MAX_PACKET_LEN) {
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        hex[i * 2] = digits[(data[i] >> 4) & 0x0F];
        hex[(i * 2) + 1] = digits[data[i] & 0x0F];
    }
    hex[len * 2] = '\0';
    ESP_LOGI(TAG, "SELFTEST_HEX %s", hex);
}

esp_err_t self_test_run(diagnostic_report_t *report, uint32_t boot_count) {
    if (report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(report, 0, sizeof(*report));
    report->version = DIAGNOSTIC_PAYLOAD_VERSION;
    report->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    report->boot_count = boot_count;

    fill_pin_map(report);
    run_nvs_check(report);
    run_rng_check(report);
    run_i2c_check(report);
    run_gnss_check(report);
    run_lora_check(report);
    finalize_status(report);

    return ESP_OK;
}

void self_test_log_report(const diagnostic_report_t *report) {
    if (report == NULL) {
        return;
    }

    ESP_LOGI(
        TAG,
        "SELFTEST %s passed=0x%04X warn=0x%04X fail=0x%04X boot=%lu uptime=%lu",
        status_name(report->overall_status),
        (unsigned)report->passed_mask,
        (unsigned)report->warning_mask,
        (unsigned)report->failed_mask,
        (unsigned long)report->boot_count,
        (unsigned long)report->uptime_s
    );
    ESP_LOGI(
        TAG,
        "SELFTEST detail lora=%d gnss=%d i2c=%d i2c_seen=%u rng=%d nvs=%d",
        (int)report->lora_status,
        (int)report->gnss_status,
        (int)report->i2c_status,
        (unsigned)report->i2c_devices_seen,
        (int)report->rng_status,
        (int)report->nvs_status
    );
    ESP_LOGI(
        TAG,
        "SELFTEST pins led=%u lora=[%u,%u,%u,%u,%u,%u,%u] gnss=[%u,%u] i2c=[%u,%u]",
        (unsigned)report->pins[0],
        (unsigned)report->pins[1],
        (unsigned)report->pins[2],
        (unsigned)report->pins[3],
        (unsigned)report->pins[4],
        (unsigned)report->pins[5],
        (unsigned)report->pins[6],
        (unsigned)report->pins[7],
        (unsigned)report->pins[8],
        (unsigned)report->pins[9],
        (unsigned)report->pins[10],
        (unsigned)report->pins[11]
    );
}

esp_err_t self_test_encode_report_packet(
    const diagnostic_report_t *report,
    uint8_t *encoded,
    size_t encoded_capacity,
    size_t *out_len,
    hope_packet_t *out_packet
) {
    if (report == NULL || encoded == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_len = 0;

    hope_packet_t pkt;
    esp_err_t err = diagnostic_protocol_build_packet(report, &pkt);
    if (err != ESP_OK) {
        return err;
    }

    pkt.src_id = CUBESAT_NODE_ID;
    pkt.dst_id = CUBESAT_GROUND_ID;
    pkt.session_id = session_get_id();
    pkt.counter = session_next_counter();
    pkt.timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    int encoded_len = packet_encode(&pkt, encoded, encoded_capacity);
    if (encoded_len <= 0) {
        return ESP_FAIL;
    }

    *out_len = (size_t)encoded_len;
    if (out_packet != NULL) {
        *out_packet = pkt;
    }
    return ESP_OK;
}

esp_err_t self_test_emit_report_packet(const diagnostic_report_t *report) {
    if (report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    hope_packet_t pkt;
    uint8_t encoded[HOPE_MAX_PACKET_LEN];
    size_t encoded_len = 0;
    esp_err_t err = self_test_encode_report_packet(report, encoded, sizeof(encoded), &encoded_len, &pkt);
    if (err != ESP_OK) {
        return err;
    }

    log_packet_hex(encoded, (size_t)encoded_len);

    if (report->lora_status != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t tx_result = lora_send(encoded, encoded_len, SELF_TEST_LORA_TX_TIMEOUT_MS);
    if (tx_result == ESP_OK) {
        (void)counter_store_save_tx(pkt.session_id, pkt.counter);
    }
    return tx_result;
}
