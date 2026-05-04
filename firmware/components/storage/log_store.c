#include "log_store.h"

#include <string.h>

#include "esp_timer.h"

#define LOG_STORE_CAPACITY 32

static log_store_record_t s_records[LOG_STORE_CAPACITY];
static size_t s_next = 0;
static size_t s_count = 0;

static void copy_message(char *dst, size_t dst_len, const char *src) {
    if (dst == NULL || dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;
    for (; i + 1 < dst_len && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

void log_store_init(void) {
    memset(s_records, 0, sizeof(s_records));
    s_next = 0;
    s_count = 0;
}

esp_err_t log_store_append(log_store_level_t level, const char *message) {
    if (message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    log_store_record_t *record = &s_records[s_next];
    record->timestamp_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    record->level = level;
    copy_message(record->message, sizeof(record->message), message);

    s_next = (s_next + 1) % LOG_STORE_CAPACITY;
    if (s_count < LOG_STORE_CAPACITY) {
        s_count++;
    }
    return ESP_OK;
}

size_t log_store_count(void) {
    return s_count;
}

esp_err_t log_store_get(size_t index, log_store_record_t *record) {
    if (record == NULL || index >= s_count) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t oldest = (s_next + LOG_STORE_CAPACITY - s_count) % LOG_STORE_CAPACITY;
    size_t slot = (oldest + index) % LOG_STORE_CAPACITY;
    *record = s_records[slot];
    return ESP_OK;
}
