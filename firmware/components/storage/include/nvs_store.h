#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t nvs_store_init(void);
esp_err_t nvs_store_set_u32(const char *ns_name, const char *key, uint32_t value);
esp_err_t nvs_store_get_u32(const char *ns_name, const char *key, uint32_t *value);
esp_err_t nvs_store_set_blob(const char *ns_name, const char *key, const void *data, size_t len);
esp_err_t nvs_store_get_blob(const char *ns_name, const char *key, void *data, size_t *len);
esp_err_t nvs_store_set_string(const char *ns_name, const char *key, const char *value);
esp_err_t nvs_store_get_string(const char *ns_name, const char *key, char *value, size_t *len);
