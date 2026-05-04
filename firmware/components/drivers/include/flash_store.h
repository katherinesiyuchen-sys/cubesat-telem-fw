#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t flash_store_init(void);
esp_err_t flash_store_write_blob(const char *key, const void *data, size_t len);
esp_err_t flash_store_read_blob(const char *key, void *data, size_t *len);
