#include "flash_store.h"

esp_err_t flash_store_init(void) {
    return ESP_OK;
}

esp_err_t flash_store_write_blob(const char *key, const void *data, size_t len) {
    (void)key;
    (void)data;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t flash_store_read_blob(const char *key, void *data, size_t *len) {
    (void)key;
    (void)data;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
}
