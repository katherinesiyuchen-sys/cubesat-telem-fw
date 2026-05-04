#include "gnss.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static gnss_config_t s_config;
static bool s_initialized = false;

static bool gnss_config_matches(const gnss_config_t *config) {
    return s_config.uart_port == config->uart_port &&
        s_config.pin_tx == config->pin_tx &&
        s_config.pin_rx == config->pin_rx &&
        s_config.baudrate == config->baudrate;
}

static int32_t coordinate_to_e7(const char *value, const char *hemisphere) {
    if (value == NULL || value[0] == '\0' || hemisphere == NULL) {
        return 0;
    }

    double raw = strtod(value, NULL);
    int degrees = (int)(raw / 100.0);
    double minutes = raw - ((double)degrees * 100.0);
    double decimal = (double)degrees + (minutes / 60.0);

    char hemi = (char)toupper((unsigned char)hemisphere[0]);
    if (hemi == 'S' || hemi == 'W') {
        decimal = -decimal;
    }

    return (int32_t)(decimal * 10000000.0);
}

static bool sentence_type_is(const char *field, const char *type) {
    size_t len = strlen(field);
    if (len < 3) {
        return false;
    }
    return strcmp(&field[len - 3], type) == 0;
}

esp_err_t gnss_init(const gnss_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        return gnss_config_matches(config) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    s_config = *config;

    uart_config_t uart_config = {
        .baud_rate = (int)s_config.baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t install_result = uart_driver_install(s_config.uart_port, 2048, 0, 0, NULL, 0);
    if (install_result != ESP_OK && install_result != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(install_result, "gnss", "uart driver install failed");
    }
    ESP_RETURN_ON_ERROR(uart_param_config(s_config.uart_port, &uart_config), "gnss", "uart config failed");
    ESP_RETURN_ON_ERROR(
        uart_set_pin(s_config.uart_port, s_config.pin_tx, s_config.pin_rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
        "gnss",
        "uart pins failed"
    );

    s_initialized = true;
    return ESP_OK;
}

esp_err_t gnss_parse_sentence(const char *sentence, gnss_fix_t *fix) {
    if (sentence == NULL || fix == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char scratch[128];
    size_t len = strnlen(sentence, sizeof(scratch) - 1);
    memcpy(scratch, sentence, len);
    scratch[len] = '\0';

    char *checksum = strchr(scratch, '*');
    if (checksum != NULL) {
        *checksum = '\0';
    }

    char *fields[16] = { 0 };
    size_t count = 0;
    char *saveptr = NULL;
    char *token = strtok_r(scratch, ",", &saveptr);
    while (token != NULL && count < (sizeof(fields) / sizeof(fields[0]))) {
        fields[count++] = token;
        token = strtok_r(NULL, ",", &saveptr);
    }

    if (count == 0 || fields[0][0] != '$') {
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(fix, 0, sizeof(*fix));

    if (sentence_type_is(fields[0], "RMC")) {
        if (count < 7 || fields[2][0] != 'A') {
            return ESP_ERR_INVALID_STATE;
        }

        fix->valid = true;
        fix->latitude_e7 = coordinate_to_e7(fields[3], fields[4]);
        fix->longitude_e7 = coordinate_to_e7(fields[5], fields[6]);
        fix->fix_type = 2;
        fix->satellites = 0;
        return ESP_OK;
    }

    if (sentence_type_is(fields[0], "GGA")) {
        if (count < 8) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        int quality = (int)strtol(fields[6], NULL, 10);
        if (quality <= 0) {
            return ESP_ERR_INVALID_STATE;
        }

        fix->valid = true;
        fix->latitude_e7 = coordinate_to_e7(fields[2], fields[3]);
        fix->longitude_e7 = coordinate_to_e7(fields[4], fields[5]);
        fix->fix_type = (uint8_t)quality;
        fix->satellites = (uint8_t)strtoul(fields[7], NULL, 10);
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t gnss_read_fix(gnss_fix_t *fix, uint32_t timeout_ms) {
    if (fix == NULL || !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char line[128];
    size_t line_len = 0;
    int64_t deadline = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);

    while (esp_timer_get_time() < deadline) {
        uint8_t ch = 0;
        int read = uart_read_bytes(s_config.uart_port, &ch, 1, pdMS_TO_TICKS(100));
        if (read <= 0) {
            continue;
        }

        if (ch == '\n') {
            line[line_len] = '\0';
            line_len = 0;

            esp_err_t result = gnss_parse_sentence(line, fix);
            if (result == ESP_OK && fix->valid) {
                return ESP_OK;
            }
            continue;
        }

        if (ch == '\r') {
            continue;
        }

        if (line_len < sizeof(line) - 1) {
            line[line_len++] = (char)ch;
        } else {
            line_len = 0;
        }
    }

    return ESP_ERR_TIMEOUT;
}
