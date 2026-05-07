#include "gnss.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GNSS_MAX_CACHED_FIX_AGE_MS 5000U

static gnss_config_t s_config;
static bool s_initialized = false;
static gnss_fix_t s_latest_fix;
static uint32_t s_latest_fix_ms;

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

static int32_t decimal_to_x10(const char *value) {
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    double parsed = strtod(value, NULL);
    return (int32_t)((parsed * 10.0) + (parsed >= 0.0 ? 0.5 : -0.5));
}

static uint16_t decimal_to_u16_x100(const char *value) {
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    double parsed = strtod(value, NULL);
    if (parsed <= 0.0) {
        return 0;
    }
    if (parsed > 655.35) {
        return UINT16_MAX;
    }
    return (uint16_t)((parsed * 100.0) + 0.5);
}

static uint16_t knots_to_mps_x100(const char *value) {
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    double knots = strtod(value, NULL);
    if (knots <= 0.0) {
        return 0;
    }
    double mps = knots * 0.514444;
    if (mps > 655.35) {
        return UINT16_MAX;
    }
    return (uint16_t)((mps * 100.0) + 0.5);
}

static uint32_t parse_utc_time_ms(const char *value) {
    if (value == NULL || strlen(value) < 6) {
        return 0;
    }
    for (size_t i = 0; i < 6; ++i) {
        if (!isdigit((unsigned char)value[i])) {
            return 0;
        }
    }

    uint32_t hours = (uint32_t)((value[0] - '0') * 10 + (value[1] - '0'));
    uint32_t minutes = (uint32_t)((value[2] - '0') * 10 + (value[3] - '0'));
    uint32_t seconds = (uint32_t)((value[4] - '0') * 10 + (value[5] - '0'));
    uint32_t milliseconds = 0;

    const char *fraction = strchr(value, '.');
    if (fraction != NULL) {
        fraction++;
        uint32_t scale = 100;
        while (*fraction != '\0' && scale > 0) {
            if (!isdigit((unsigned char)*fraction)) {
                break;
            }
            milliseconds += (uint32_t)(*fraction - '0') * scale;
            scale /= 10;
            fraction++;
        }
    }

    if (hours > 23 || minutes > 59 || seconds > 59) {
        return 0;
    }
    return (((hours * 60U) + minutes) * 60U + seconds) * 1000U + milliseconds;
}

static uint32_t parse_utc_date_ddmmyy(const char *value) {
    if (value == NULL || strlen(value) < 6) {
        return 0;
    }
    for (size_t i = 0; i < 6; ++i) {
        if (!isdigit((unsigned char)value[i])) {
            return 0;
        }
    }
    return (uint32_t)strtoul(value, NULL, 10);
}

static bool sentence_type_is(const char *field, const char *type) {
    size_t len = strlen(field);
    if (len < 3) {
        return false;
    }
    return strcmp(&field[len - 3], type) == 0;
}

static int hex_value(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    value = (char)toupper((unsigned char)value);
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static esp_err_t validate_nmea_checksum(const char *sentence, bool *had_checksum) {
    if (sentence == NULL || had_checksum == NULL || sentence[0] != '$') {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *had_checksum = false;
    const char *star = strchr(sentence, '*');
    if (star == NULL) {
        return ESP_OK;
    }
    if (star[1] == '\0' || star[2] == '\0') {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t checksum = 0;
    for (const char *cursor = sentence + 1; cursor < star; ++cursor) {
        checksum ^= (uint8_t)*cursor;
    }

    int hi = hex_value(star[1]);
    int lo = hex_value(star[2]);
    if (hi < 0 || lo < 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *had_checksum = true;
    return checksum == (uint8_t)((hi << 4) | lo) ? ESP_OK : ESP_ERR_INVALID_CRC;
}

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void merge_fix(const gnss_fix_t *update) {
    if (update == NULL || !update->valid) {
        return;
    }

    if (!s_latest_fix.valid) {
        memset(&s_latest_fix, 0, sizeof(s_latest_fix));
    }

    s_latest_fix.valid = true;
    s_latest_fix.latitude_e7 = update->latitude_e7;
    s_latest_fix.longitude_e7 = update->longitude_e7;
    s_latest_fix.utc_time_ms = update->utc_time_ms != 0 ? update->utc_time_ms : s_latest_fix.utc_time_ms;
    s_latest_fix.source_flags |= update->source_flags;

    if ((update->source_flags & GNSS_FIX_FLAG_GGA) != 0) {
        s_latest_fix.fix_type = update->fix_type;
        s_latest_fix.satellites = update->satellites;
        s_latest_fix.hdop_x100 = update->hdop_x100;
        s_latest_fix.altitude_m_x10 = update->altitude_m_x10;
    }

    if ((update->source_flags & GNSS_FIX_FLAG_RMC) != 0) {
        if (s_latest_fix.fix_type == 0) {
            s_latest_fix.fix_type = update->fix_type;
        }
        s_latest_fix.speed_mps_x100 = update->speed_mps_x100;
        s_latest_fix.course_deg_x100 = update->course_deg_x100;
        s_latest_fix.utc_date_ddmmyy = update->utc_date_ddmmyy;
    }

    s_latest_fix_ms = now_ms();
    s_latest_fix.fix_age_ms = 0;
    s_latest_fix.source_flags &= (uint8_t)~GNSS_FIX_FLAG_STALE;
}

static esp_err_t latest_fix(gnss_fix_t *fix) {
    if (fix == NULL || !s_latest_fix.valid) {
        return ESP_ERR_INVALID_STATE;
    }

    *fix = s_latest_fix;
    uint32_t age = now_ms() - s_latest_fix_ms;
    fix->fix_age_ms = age;
    if (age > GNSS_MAX_CACHED_FIX_AGE_MS) {
        fix->source_flags |= GNSS_FIX_FLAG_STALE;
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
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

    bool had_checksum = false;
    esp_err_t checksum_result = validate_nmea_checksum(sentence, &had_checksum);
    if (checksum_result != ESP_OK) {
        return checksum_result;
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
    char *cursor = scratch;
    while (count < (sizeof(fields) / sizeof(fields[0]))) {
        fields[count++] = cursor;
        char *comma = strchr(cursor, ',');
        if (comma == NULL) {
            break;
        }
        *comma = '\0';
        cursor = comma + 1;
    }

    if (count == 0 || fields[0][0] != '$') {
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(fix, 0, sizeof(*fix));
    if (had_checksum) {
        fix->source_flags |= GNSS_FIX_FLAG_CHECKSUM;
    }

    if (sentence_type_is(fields[0], "RMC")) {
        if (count < 7 || fields[2][0] != 'A') {
            return ESP_ERR_INVALID_STATE;
        }

        fix->valid = true;
        fix->latitude_e7 = coordinate_to_e7(fields[3], fields[4]);
        fix->longitude_e7 = coordinate_to_e7(fields[5], fields[6]);
        fix->fix_type = 2;
        fix->satellites = 0;
        fix->speed_mps_x100 = count > 7 ? knots_to_mps_x100(fields[7]) : 0;
        fix->course_deg_x100 = count > 8 ? decimal_to_u16_x100(fields[8]) : 0;
        fix->utc_date_ddmmyy = count > 9 ? parse_utc_date_ddmmyy(fields[9]) : 0;
        fix->utc_time_ms = parse_utc_time_ms(fields[1]);
        fix->source_flags |= GNSS_FIX_FLAG_RMC;
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
        fix->hdop_x100 = count > 8 ? decimal_to_u16_x100(fields[8]) : 0;
        fix->altitude_m_x10 = count > 9 ? decimal_to_x10(fields[9]) : 0;
        fix->utc_time_ms = parse_utc_time_ms(fields[1]);
        fix->source_flags |= GNSS_FIX_FLAG_GGA;
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
                merge_fix(fix);
                return latest_fix(fix);
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

    return latest_fix(fix);
}
