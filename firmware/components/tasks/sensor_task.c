#include "sensor_task.h"

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_i2c.h"
#include "imu.h"
#include "temp_sensor.h"

static const char *TAG = "sensor_task";
static const uint32_t SENSOR_I2C_TIMEOUT_MS = 50;
static const uint32_t SENSOR_TASK_PERIOD_MS = 5000;
static const uint32_t SENSOR_SCAN_TIMEOUT_MS = 10;
static const uint8_t GNSS_I2C_ADDR = 0x42;
static const uint8_t ATECC608_I2C_ADDR = 0x60;
static const uint8_t MCP23017_I2C_ADDR = 0x20;
static const uint8_t BME688_I2C_ADDR = 0x76;
static const uint8_t BME688_I2C_ADDR_ALT = 0x77;
static const uint8_t BME688_REG_CHIP_ID = 0xD0;
static const uint8_t VEML7700_I2C_ADDR = 0x10;
static const uint8_t VEML7700_REG_ALS = 0x04;
static const uint8_t LIS2MDL_I2C_ADDR = 0x1E;
static const uint8_t LIS2MDL_REG_WHO_AM_I = 0x4F;
static const uint8_t DS3231_I2C_ADDR = 0x68;
static const uint8_t DS3231_REG_STATUS = 0x0F;

typedef struct {
    bool valid;
    int16_t temperature_c_x10;
} sensor_cache_t;

static sensor_cache_t s_sensor_cache = { 0 };
static portMUX_TYPE s_sensor_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_tmp117_failure_logged = false;

typedef enum {
    SENSOR_PROBE_RTC = 0,
    SENSOR_PROBE_BME688,
    SENSOR_PROBE_VEML7700,
    SENSOR_PROBE_TMP117,
    SENSOR_PROBE_LIS2MDL,
    SENSOR_PROBE_GNSS_I2C,
    SENSOR_PROBE_MCP23017,
    SENSOR_PROBE_ATECC608,
    SENSOR_PROBE_COUNT
} sensor_probe_stage_t;

static sensor_probe_stage_t s_probe_stage = SENSOR_PROBE_RTC;

static esp_err_t sensor_i2c_init(void) {
    board_i2c_bus_snapshot_t bus = { 0 };
    esp_err_t snapshot_result = board_get_i2c_bus_snapshot(&bus);
    if (snapshot_result != ESP_OK) {
        return snapshot_result;
    }

    const hal_i2c_config_t config = {
        .port = HAL_I2C_PORT_0,
        .sda_pin = (gpio_num_t)bus.sda_pin,
        .scl_pin = (gpio_num_t)bus.scl_pin,
        .clock_hz = bus.clock_hz,
    };
    ESP_LOGI(TAG, "sensor i2c using SDA=%d SCL=%d swapped=%d",
        bus.sda_pin,
        bus.scl_pin,
        bus.swapped_from_schematic ? 1 : 0);
    return hal_i2c_master_init(&config);
}

static esp_err_t sensor_read_u8(uint8_t addr, uint8_t reg, uint8_t *value) {
    return hal_i2c_write_read(addr, &reg, 1, value, 1, SENSOR_I2C_TIMEOUT_MS);
}

static esp_err_t sensor_read_u16_le(uint8_t addr, uint8_t reg, uint16_t *value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw[2] = { 0 };
    esp_err_t err = hal_i2c_write_read(addr, &reg, 1, raw, sizeof(raw), SENSOR_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    *value = ((uint16_t)raw[1] << 8) | raw[0];
    return ESP_OK;
}

static void sensor_probe_addr(uint8_t addr, const char *label) {
    esp_err_t reset_result = hal_i2c_bus_reset();
    if (reset_result != ESP_OK) {
        ESP_LOGW(TAG, "i2c bus reset before %s probe failed: %s", label, esp_err_to_name(reset_result));
    }

    esp_err_t err = hal_i2c_probe(addr, SENSOR_SCAN_TIMEOUT_MS);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "i2c ack addr=0x%02X label=%s", addr, label);
    } else {
        ESP_LOGI(TAG, "i2c miss addr=0x%02X label=%s err=%s", addr, label, esp_err_to_name(err));
    }
}

static void sensor_run_probe_stage(void) {
    switch (s_probe_stage) {
        case SENSOR_PROBE_RTC: {
            uint8_t status = 0;
            esp_err_t err = sensor_read_u8(DS3231_I2C_ADDR, DS3231_REG_STATUS, &status);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "probe rtc addr=0x%02X status=0x%02X", DS3231_I2C_ADDR, status);
            } else {
                ESP_LOGI(TAG, "probe rtc addr=0x%02X failed: %s", DS3231_I2C_ADDR, esp_err_to_name(err));
            }
            break;
        }
        case SENSOR_PROBE_BME688: {
            uint8_t chip_id = 0;
            esp_err_t err = sensor_read_u8(BME688_I2C_ADDR, BME688_REG_CHIP_ID, &chip_id);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "probe bme688 addr=0x%02X chip_id=0x%02X", BME688_I2C_ADDR, chip_id);
            } else {
                err = sensor_read_u8(BME688_I2C_ADDR_ALT, BME688_REG_CHIP_ID, &chip_id);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "probe bme688 addr=0x%02X chip_id=0x%02X", BME688_I2C_ADDR_ALT, chip_id);
                } else {
                    ESP_LOGI(TAG, "probe bme688 failed: %s", esp_err_to_name(err));
                }
            }
            break;
        }
        case SENSOR_PROBE_VEML7700: {
            uint16_t als_raw = 0;
            esp_err_t err = sensor_read_u16_le(VEML7700_I2C_ADDR, VEML7700_REG_ALS, &als_raw);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "probe veml7700 addr=0x%02X als_raw=%u", VEML7700_I2C_ADDR, (unsigned int)als_raw);
            } else {
                ESP_LOGI(TAG, "probe veml7700 addr=0x%02X failed: %s", VEML7700_I2C_ADDR, esp_err_to_name(err));
            }
            break;
        }
        case SENSOR_PROBE_TMP117: {
            int16_t temperature_c_x10 = 0;
            esp_err_t err = temp_sensor_read_c_x10(&temperature_c_x10);
            if (err == ESP_OK) {
                taskENTER_CRITICAL(&s_sensor_lock);
                s_sensor_cache.valid = true;
                s_sensor_cache.temperature_c_x10 = temperature_c_x10;
                taskEXIT_CRITICAL(&s_sensor_lock);
                s_tmp117_failure_logged = false;
                ESP_LOGI(TAG, "probe tmp117 temperature=%d.%dC",
                    temperature_c_x10 / 10,
                    temperature_c_x10 >= 0 ? temperature_c_x10 % 10 : -(temperature_c_x10 % 10));
            } else {
                taskENTER_CRITICAL(&s_sensor_lock);
                s_sensor_cache.valid = false;
                s_sensor_cache.temperature_c_x10 = 0;
                taskEXIT_CRITICAL(&s_sensor_lock);
                if (!s_tmp117_failure_logged) {
                    ESP_LOGW(TAG, "probe tmp117 failed: %s", esp_err_to_name(err));
                    s_tmp117_failure_logged = true;
                }
            }
            break;
        }
        case SENSOR_PROBE_LIS2MDL: {
            uint8_t who_am_i = 0;
            esp_err_t err = sensor_read_u8(LIS2MDL_I2C_ADDR, LIS2MDL_REG_WHO_AM_I, &who_am_i);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "probe lis2mdl addr=0x%02X whoami=0x%02X", LIS2MDL_I2C_ADDR, who_am_i);
            } else {
                ESP_LOGI(TAG, "probe lis2mdl addr=0x%02X failed: %s", LIS2MDL_I2C_ADDR, esp_err_to_name(err));
            }
            break;
        }
        case SENSOR_PROBE_GNSS_I2C:
            sensor_probe_addr(GNSS_I2C_ADDR, "max-m10s");
            break;
        case SENSOR_PROBE_MCP23017:
            sensor_probe_addr(MCP23017_I2C_ADDR, "mcp23017");
            break;
        case SENSOR_PROBE_ATECC608:
            sensor_probe_addr(ATECC608_I2C_ADDR, "atecc608");
            break;
        default:
            break;
    }

    s_probe_stage = (sensor_probe_stage_t)(((int)s_probe_stage + 1) % SENSOR_PROBE_COUNT);
}

static void sensor_log_mcp_snapshot(void) {
    board_alert_snapshot_t snapshot = { 0 };
    if (board_get_alert_snapshot(&snapshot) == ESP_OK) {
        ESP_LOGI(
            TAG,
            "extender gpioa=0x%02X gpiob=0x%02X battery=%d mag_drdy=%d power=%d temp=%d solar=%d usb=%d",
            snapshot.porta_state,
            snapshot.portb_state,
            snapshot.battery_alert_level,
            snapshot.mag_drdy_level,
            snapshot.power_alert_level,
            snapshot.temp_alert_level,
            snapshot.solar_alert_level,
            snapshot.usb_power_alert_level
        );
    }
}

static void sensor_task(void *arg) {
    (void)arg;

    esp_err_t i2c_result = sensor_i2c_init();
    if (i2c_result != ESP_OK) {
        ESP_LOGW(TAG, "sensor i2c init failed: %s", esp_err_to_name(i2c_result));
    }

    esp_err_t imu_result = imu_init();
    if (imu_result == ESP_OK) {
        ESP_LOGI(TAG, "bno085 control path initialized");
    } else {
        ESP_LOGW(TAG, "bno085 init failed: %s", esp_err_to_name(imu_result));
    }

    while (1) {
        sensor_run_probe_stage();
        sensor_log_mcp_snapshot();

        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

void sensor_task_start(void) {
    BaseType_t started = xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 4, NULL);
    if (started == pdPASS) {
        ESP_LOGI(TAG, "sensor task online");
    } else {
        ESP_LOGE(TAG, "sensor task start failed");
    }
}

esp_err_t sensor_task_get_latest_temperature_c_x10(int16_t *temperature_c_x10) {
    if (temperature_c_x10 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_sensor_lock);
    bool valid = s_sensor_cache.valid;
    int16_t value = s_sensor_cache.temperature_c_x10;
    taskEXIT_CRITICAL(&s_sensor_lock);

    if (!valid) {
        return ESP_ERR_NOT_FOUND;
    }

    *temperature_c_x10 = value;
    return ESP_OK;
}
