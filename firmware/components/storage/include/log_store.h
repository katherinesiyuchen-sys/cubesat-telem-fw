#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    LOG_STORE_INFO = 0,
    LOG_STORE_WARN = 1,
    LOG_STORE_ERROR = 2,
} log_store_level_t;

typedef struct {
    uint32_t timestamp_s;
    log_store_level_t level;
    char message[96];
} log_store_record_t;

void log_store_init(void);
esp_err_t log_store_append(log_store_level_t level, const char *message);
size_t log_store_count(void);
esp_err_t log_store_get(size_t index, log_store_record_t *record);
