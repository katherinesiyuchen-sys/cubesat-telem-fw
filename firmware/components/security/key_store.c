#include "key_store.h"

#include <stdbool.h>
#include <string.h>

#include "nvs.h"
#include "nvs_store.h"

#define KEY_STORE_NS "cubesat_key"
#define KEY_STORE_MATERIAL_KEY "material"

static key_store_material_t s_demo_keys;
static bool s_has_demo_keys = false;

esp_err_t key_store_init(void) {
    size_t len = sizeof(s_demo_keys);
    esp_err_t err = nvs_store_get_blob(KEY_STORE_NS, KEY_STORE_MATERIAL_KEY, &s_demo_keys, &len);
    if (err == ESP_OK && len == sizeof(s_demo_keys) && s_demo_keys.provisioned) {
        s_has_demo_keys = true;
        return ESP_OK;
    }

    memset(&s_demo_keys, 0, sizeof(s_demo_keys));
    s_has_demo_keys = false;
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_NOT_INITIALIZED) {
        return ESP_OK;
    }
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
    s_demo_keys.provisioned = true;
    s_has_demo_keys = true;
    return nvs_store_set_blob(KEY_STORE_NS, KEY_STORE_MATERIAL_KEY, &s_demo_keys, sizeof(s_demo_keys));
}
