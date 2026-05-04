#include "nvs_store.h"

#include "nvs.h"
#include "nvs_flash.h"

esp_err_t nvs_store_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t open_rw(const char *ns_name, nvs_handle_t *handle) {
    if (ns_name == NULL || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_open(ns_name, NVS_READWRITE, handle);
}

esp_err_t nvs_store_set_u32(const char *ns_name, const char *key, uint32_t value) {
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = open_rw(ns_name, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_store_get_u32(const char *ns_name, const char *key, uint32_t *value) {
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = open_rw(ns_name, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_u32(handle, key, value);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_store_set_blob(const char *ns_name, const char *key, const void *data, size_t len) {
    if (key == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = open_rw(ns_name, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_store_get_blob(const char *ns_name, const char *key, void *data, size_t *len) {
    if (key == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = open_rw(ns_name, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_blob(handle, key, data, len);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_store_set_string(const char *ns_name, const char *key, const char *value) {
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = open_rw(ns_name, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_store_get_string(const char *ns_name, const char *key, char *value, size_t *len) {
    if (key == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = open_rw(ns_name, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(handle, key, value, len);
    nvs_close(handle);
    return err;
}
