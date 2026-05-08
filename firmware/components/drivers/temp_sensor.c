#include "temp_sensor.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "hal_i2c.h"

static const char *TAG = "temp_sensor";
static const uint8_t TMP117_REG_TEMP = 0x00;
static const uint8_t TMP117_REG_DEVICE_ID = 0x0F;
static const uint16_t TMP117_DEVICE_ID = 0x0117;
static const uint32_t TMP117_I2C_TIMEOUT_MS = 50;

static bool s_tmp117_ready = false;

static esp_err_t tmp117_read_u16(uint8_t reg, uint16_t *value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw[2] = { 0 };
    esp_err_t err = hal_i2c_write_read(TMP117_I2C_ADDR, &reg, 1, raw, sizeof(raw), TMP117_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    *value = ((uint16_t)raw[0] << 8) | raw[1];
    return ESP_OK;
}

esp_err_t temp_sensor_init(void) {
    uint16_t device_id = 0;
    esp_err_t err = tmp117_read_u16(TMP117_REG_DEVICE_ID, &device_id);
    if (err != ESP_OK) {
        s_tmp117_ready = false;
        return err;
    }
    if (device_id != TMP117_DEVICE_ID) {
        s_tmp117_ready = false;
        ESP_LOGW(TAG, "TMP117 unexpected device id 0x%04X (expected 0x%04X)", device_id, TMP117_DEVICE_ID);
        return ESP_ERR_INVALID_RESPONSE;
    }

    s_tmp117_ready = true;
    ESP_LOGI(TAG, "TMP117 online at 0x%02X device_id=0x%04X", TMP117_I2C_ADDR, device_id);
    return ESP_OK;
}

esp_err_t temp_sensor_read_c_x10(int16_t *temperature_c_x10) {
    if (temperature_c_x10 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_tmp117_ready) {
        esp_err_t init_result = temp_sensor_init();
        if (init_result != ESP_OK) {
            return init_result;
        }
    }

    uint16_t raw = 0;
    esp_err_t err = tmp117_read_u16(TMP117_REG_TEMP, &raw);
    if (err != ESP_OK) {
        return err;
    }

    int16_t raw_temp = (int16_t)raw;
    int32_t milli_c = ((int32_t)raw_temp * 1000) / 128;
    *temperature_c_x10 = (int16_t)(milli_c / 100);
    return ESP_OK;
}
