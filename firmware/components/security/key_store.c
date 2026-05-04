#include "key_store.h"

#include <stdbool.h>
#include <string.h>

static key_store_material_t s_demo_keys;
static bool s_has_demo_keys = false;

esp_err_t key_store_init(void) {
    memset(&s_demo_keys, 0, sizeof(s_demo_keys));
    s_has_demo_keys = false;
    return ESP_OK;
}

esp_err_t key_store_load(key_store_material_t *keys) {
    if (keys == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_has_demo_keys) {
        memset(keys, 0, sizeof(*keys));
        return ESP_ERR_NOT_FOUND;
    }

    *keys = s_demo_keys;
    return ESP_OK;
}

esp_err_t key_store_save(const key_store_material_t *keys) {
    if (keys == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_demo_keys = *keys;
    s_has_demo_keys = true;
    return ESP_OK;
}
