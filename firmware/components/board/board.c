#include "board.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_i2c.h"

static const char *TAG = "board";
static const uint32_t BOARD_I2C_CLOCK_HZ = 50000;
static const uint32_t BOARD_I2C_TIMEOUT_MS = 100;
static const uint8_t MCP23017_REG_IODIRA = 0x00;
static const uint8_t MCP23017_REG_IODIRB = 0x01;
static const uint8_t MCP23017_REG_GPINTENB = 0x05;
static const uint8_t MCP23017_REG_INTCONB = 0x09;
static const uint8_t MCP23017_REG_IOCON = 0x0A;
static const uint8_t MCP23017_REG_IOCON_ALT = 0x0B;
static const uint8_t MCP23017_REG_GPPUB = 0x0D;
static const uint8_t MCP23017_REG_GPIOA = 0x12;
static const uint8_t MCP23017_REG_GPIOB = 0x13;
static const uint8_t MCP23017_ALERT_MASK = 0x3F;
static const uint8_t MCP23017_IOCON_MIRROR = 0x40;

static board_alert_snapshot_t s_alert_snapshot;
static uint8_t s_mcp23017_addr = CUBESAT_MCP23017_I2C_ADDR;
static int s_i2c_sda_pin = PIN_I2C_SDA;
static int s_i2c_scl_pin = PIN_I2C_SCL;
static bool s_i2c_swapped_from_schematic = false;

static void board_log_optional_gpio(const char *label, int pin) {
    if (pin >= 0) {
        ESP_LOGI(TAG, "%s=%d", label, pin);
    }
}

static uint64_t board_gpio_bit_mask(int pin) {
    return pin < 0 ? 0ULL : (1ULL << (uint32_t)pin);
}

static void board_configure_optional_output(int pin, int initial_level, const char *label) {
    if (pin < 0) {
        return;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = board_gpio_bit_mask(pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(pin, initial_level ? 1 : 0);
    ESP_LOGI(TAG, "%s set to %d", label, initial_level ? 1 : 0);
}

static void board_configure_spi_idle_select(int pin, const char *label) {
    if (pin < 0) {
        return;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = board_gpio_bit_mask(pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(pin, 1);
    ESP_LOGI(TAG, "%s idle high", label);
}

static void board_log_optional_hold(const char *label, int pin) {
    if (pin >= 0) {
        ESP_LOGI(TAG, "%s left unchanged for bring-up", label);
    }
}

static void board_alert_snapshot_from_ports(uint8_t porta_state, uint8_t portb_state, bool present) {
    s_alert_snapshot.expander_present = present;
    s_alert_snapshot.porta_state = porta_state;
    s_alert_snapshot.portb_state = portb_state;
    s_alert_snapshot.battery_alert_level = (portb_state & 0x01) != 0;
    s_alert_snapshot.mag_drdy_level = (portb_state & 0x02) != 0;
    s_alert_snapshot.power_alert_level = (portb_state & 0x04) != 0;
    s_alert_snapshot.temp_alert_level = (portb_state & 0x08) != 0;
    s_alert_snapshot.solar_alert_level = (portb_state & 0x10) != 0;
    s_alert_snapshot.usb_power_alert_level = (portb_state & 0x20) != 0;
}

static esp_err_t board_i2c_write_reg_at(uint8_t addr, uint8_t reg, uint8_t value) {
    const uint8_t payload[] = { reg, value };
    return hal_i2c_write(addr, payload, sizeof(payload), BOARD_I2C_TIMEOUT_MS);
}

static esp_err_t board_i2c_read_reg_at(uint8_t addr, uint8_t reg, uint8_t *value) {
    return hal_i2c_write_read(addr, &reg, sizeof(reg), value, 1, BOARD_I2C_TIMEOUT_MS);
}

static esp_err_t board_i2c_write_reg(uint8_t reg, uint8_t value) {
    return board_i2c_write_reg_at(s_mcp23017_addr, reg, value);
}

static esp_err_t board_i2c_read_reg(uint8_t reg, uint8_t *value) {
    return board_i2c_read_reg_at(s_mcp23017_addr, reg, value);
}

static esp_err_t board_i2c_init_with_pins(int sda_pin, int scl_pin) {
    const hal_i2c_config_t config = {
        .port = HAL_I2C_PORT_0,
        .sda_pin = (gpio_num_t)sda_pin,
        .scl_pin = (gpio_num_t)scl_pin,
        .clock_hz = BOARD_I2C_CLOCK_HZ,
    };
    esp_err_t err = hal_i2c_master_init(&config);
    if (err == ESP_OK) {
        s_i2c_sda_pin = sda_pin;
        s_i2c_scl_pin = scl_pin;
        s_i2c_swapped_from_schematic = (sda_pin != PIN_I2C_SDA || scl_pin != PIN_I2C_SCL);
    }
    return err;
}

static size_t board_log_i2c_scan(void) {
    static const uint8_t candidate_addresses[] = { 0x10, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x42, 0x48, 0x49, 0x4A, 0x4B, 0x60, 0x68, 0x76, 0x77 };
    size_t seen = 0;
    for (size_t i = 0; i < sizeof(candidate_addresses); ++i) {
        uint8_t addr = candidate_addresses[i];
        if (hal_i2c_probe(addr, BOARD_I2C_TIMEOUT_MS) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device present at 0x%02X", addr);
            seen++;
        }
    }
    return seen;
}

static bool board_mcp23017_looks_valid_at(uint8_t addr, uint8_t *out_iodira, uint8_t *out_iodirb) {
    uint8_t iodira = 0;
    uint8_t iodirb = 0;

    if (board_i2c_read_reg_at(addr, MCP23017_REG_IODIRA, &iodira) != ESP_OK) {
        return false;
    }
    if (board_i2c_read_reg_at(addr, MCP23017_REG_IODIRB, &iodirb) != ESP_OK) {
        return false;
    }

    if (out_iodira != NULL) {
        *out_iodira = iodira;
    }
    if (out_iodirb != NULL) {
        *out_iodirb = iodirb;
    }
    return true;
}

static esp_err_t board_detect_mcp23017_address(void) {
    uint8_t iodira = 0;
    uint8_t iodirb = 0;
    if (board_mcp23017_looks_valid_at(CUBESAT_MCP23017_I2C_ADDR, &iodira, &iodirb)) {
        s_mcp23017_addr = CUBESAT_MCP23017_I2C_ADDR;
        ESP_LOGI(TAG, "MCP23017 candidate at configured address 0x%02X IODIRA=0x%02X IODIRB=0x%02X",
            s_mcp23017_addr, iodira, iodirb);
        return ESP_OK;
    }

    for (uint8_t addr = 0x20; addr <= 0x27; ++addr) {
        if (addr == CUBESAT_MCP23017_I2C_ADDR) {
            continue;
        }
        if (board_mcp23017_looks_valid_at(addr, &iodira, &iodirb)) {
            s_mcp23017_addr = addr;
            ESP_LOGW(TAG, "MCP23017 found at alternate address 0x%02X IODIRA=0x%02X IODIRB=0x%02X",
                s_mcp23017_addr, iodira, iodirb);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t board_verify_mcp23017_registers(void) {
    uint8_t iodira = 0;
    uint8_t iodirb = 0;
    esp_err_t err = board_i2c_read_reg(MCP23017_REG_IODIRA, &iodira);
    if (err != ESP_OK) {
        return err;
    }
    err = board_i2c_read_reg(MCP23017_REG_IODIRB, &iodirb);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "MCP23017 verify addr=0x%02X IODIRA=0x%02X IODIRB=0x%02X", s_mcp23017_addr, iodira, iodirb);
    return ESP_OK;
}

static esp_err_t board_configure_io_expander_interrupts(void) {
    if (PIN_MCP23017_INTA < 0 && PIN_MCP23017_INTB < 0) {
        return ESP_OK;
    }

    esp_err_t err = board_i2c_write_reg(MCP23017_REG_IOCON, MCP23017_IOCON_MIRROR);
    if (err != ESP_OK) {
        err = board_i2c_write_reg(MCP23017_REG_IOCON_ALT, MCP23017_IOCON_MIRROR);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = board_i2c_write_reg(MCP23017_REG_INTCONB, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    return board_i2c_write_reg(MCP23017_REG_GPINTENB, MCP23017_ALERT_MASK);
}

static esp_err_t board_init_io_expander(void) {
    esp_err_t err = board_i2c_init_with_pins(PIN_I2C_SDA, PIN_I2C_SCL);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "I2C idle levels sda=%d scl=%d clock=%luHz",
        gpio_get_level((gpio_num_t)s_i2c_sda_pin),
        gpio_get_level((gpio_num_t)s_i2c_scl_pin),
        (unsigned long)BOARD_I2C_CLOCK_HZ);

    size_t seen_normal = board_log_i2c_scan();

    err = board_detect_mcp23017_address();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MCP23017 not valid on schematic I2C pins after seeing %u candidate ACKs; trying swapped SDA/SCL",
            (unsigned)seen_normal);
        (void)hal_i2c_master_deinit();
        err = board_i2c_init_with_pins(PIN_I2C_SCL, PIN_I2C_SDA);
        if (err != ESP_OK) {
            return err;
        }
        ESP_LOGW(TAG, "I2C fallback using swapped pins SDA=%d SCL=%d", s_i2c_sda_pin, s_i2c_scl_pin);
        ESP_LOGI(TAG, "I2C idle levels sda=%d scl=%d clock=%luHz",
            gpio_get_level((gpio_num_t)s_i2c_sda_pin),
            gpio_get_level((gpio_num_t)s_i2c_scl_pin),
            (unsigned long)BOARD_I2C_CLOCK_HZ);
        size_t seen_swapped = board_log_i2c_scan();
        err = board_detect_mcp23017_address();
        if (err != ESP_OK) {
            if (seen_swapped > seen_normal) {
                ESP_LOGW(TAG, "Keeping swapped I2C pins because they saw %u ACKs versus %u on schematic pins",
                    (unsigned)seen_swapped,
                    (unsigned)seen_normal);
            } else {
                (void)hal_i2c_master_deinit();
                (void)board_i2c_init_with_pins(PIN_I2C_SDA, PIN_I2C_SCL);
                s_i2c_swapped_from_schematic = false;
            }
            return err;
        }
    }

    err = board_verify_mcp23017_registers();
    if (err != ESP_OK) {
        return err;
    }

    err = board_i2c_write_reg(MCP23017_REG_IODIRA, 0xFF);
    if (err != ESP_OK) {
        return err;
    }
    err = board_i2c_write_reg(MCP23017_REG_IODIRB, 0xFF);
    if (err != ESP_OK) {
        return err;
    }
    err = board_i2c_write_reg(MCP23017_REG_GPPUB, 0x00);
    if (err != ESP_OK) {
        return err;
    }

    err = board_verify_mcp23017_registers();
    if (err != ESP_OK) {
        return err;
    }

    if (PIN_MCP23017_INTA >= 0) {
        gpio_config_t int_cfg = {
            .pin_bit_mask = board_gpio_bit_mask(PIN_MCP23017_INTA),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "MCP23017 INTA gpio config failed");
    }
    if (PIN_MCP23017_INTB >= 0) {
        gpio_config_t int_cfg = {
            .pin_bit_mask = board_gpio_bit_mask(PIN_MCP23017_INTB),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "MCP23017 INTB gpio config failed");
    }

    return board_configure_io_expander_interrupts();
}

esp_err_t board_get_alert_snapshot(board_alert_snapshot_t *out_snapshot) {
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_alert_snapshot.expander_present) {
        *out_snapshot = s_alert_snapshot;
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t porta_state = 0;
    uint8_t portb_state = 0;
    esp_err_t err = board_i2c_read_reg(MCP23017_REG_GPIOA, &porta_state);
    if (err != ESP_OK) {
        return err;
    }
    err = board_i2c_read_reg(MCP23017_REG_GPIOB, &portb_state);
    if (err != ESP_OK) {
        return err;
    }

    board_alert_snapshot_from_ports(porta_state, portb_state, true);
    *out_snapshot = s_alert_snapshot;
    return ESP_OK;
}

esp_err_t board_get_i2c_bus_snapshot(board_i2c_bus_snapshot_t *out_snapshot) {
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out_snapshot->sda_pin = s_i2c_sda_pin;
    out_snapshot->scl_pin = s_i2c_scl_pin;
    out_snapshot->clock_hz = BOARD_I2C_CLOCK_HZ;
    out_snapshot->swapped_from_schematic = s_i2c_swapped_from_schematic;
    return ESP_OK;
}

static void board_log_config(void) {
    board_alert_snapshot_t snapshot = { 0 };

    ESP_LOGI(
        TAG,
        "CubeSat IDs: node=%d ground=%d session=0x%08lX",
        CUBESAT_NODE_ID,
        CUBESAT_GROUND_ID,
        (unsigned long)CUBESAT_DEMO_SESSION_ID
    );
    ESP_LOGI(
        TAG,
        "SX1262 pins: MOSI=%d MISO=%d SCLK=%d CS=%d RESET=%d BUSY=%d DIO1=%d",
        PIN_LORA_MOSI,
        PIN_LORA_MISO,
        PIN_LORA_SCLK,
        PIN_LORA_CS,
        PIN_LORA_RESET,
        PIN_LORA_BUSY,
        PIN_LORA_DIO1
    );
    ESP_LOGI(
        TAG,
        "GNSS pins: TX=%d RX=%d baud=%d; LoRa frequency=%lu Hz",
        PIN_GNSS_TX,
        PIN_GNSS_RX,
        CUBESAT_GNSS_BAUDRATE,
        (unsigned long)CUBESAT_LORA_FREQUENCY_HZ
    );
    ESP_LOGI(TAG, "I2C bus: SDA=%d SCL=%d schematic=[%d,%d] swapped=%d MCP23017(config=0x%02X active=0x%02X)",
        s_i2c_sda_pin,
        s_i2c_scl_pin,
        PIN_I2C_SDA,
        PIN_I2C_SCL,
        s_i2c_swapped_from_schematic ? 1 : 0,
        CUBESAT_MCP23017_I2C_ADDR,
        s_mcp23017_addr);
    board_log_optional_gpio("LORA_ENABLE", PIN_LORA_ENABLE);
    board_log_optional_gpio("SENSORS_ENABLE", PIN_SENSORS_ENABLE);
    board_log_optional_gpio("IMU_CS", PIN_IMU_CS);
    board_log_optional_gpio("IMU_INTERRUPT", PIN_IMU_INTERRUPT);
    board_log_optional_gpio("IMU_RESET", PIN_IMU_RESET);
    board_log_optional_gpio("IMU_WAKE", PIN_IMU_WAKE);
    board_log_optional_gpio("GNSS_EXTINT", PIN_GNSS_EXTINT);
    board_log_optional_gpio("GNSS_EN", PIN_GNSS_EN);
    board_log_optional_gpio("GNSS_RESET", PIN_GNSS_RESET);
    board_log_optional_gpio("RTC_INT", PIN_RTC_INT);
    board_log_optional_gpio("SOLAR_IRRADIANCE", PIN_SOLAR_IRRADIANCE);
    board_log_optional_gpio("USBC_ENABLE", PIN_USBC_ENABLE);
    board_log_optional_gpio("FLASH_CS", PIN_FLASH_CS);
    board_log_optional_gpio("MCP23017_INTA", PIN_MCP23017_INTA);
    board_log_optional_gpio("MCP23017_INTB", PIN_MCP23017_INTB);
    ESP_LOGI(
        TAG,
        "MCP23017 logical inputs: BATTERY_ALERT=GPB0 MAG_DRDY=GPB1 POWER_ALERT=GPB2 TEMP_ALERT=GPB3 SOLAR_ALERT=GPB4 USB_POWER_ALERT=GPB5"
    );
    if (board_get_alert_snapshot(&snapshot) == ESP_OK) {
        ESP_LOGI(
            TAG,
            "MCP23017 GPIOA=0x%02X GPIOB=0x%02X (battery=%d mag_drdy=%d power=%d temp=%d solar=%d usb=%d)",
            snapshot.porta_state,
            snapshot.portb_state,
            snapshot.battery_alert_level,
            snapshot.mag_drdy_level,
            snapshot.power_alert_level,
            snapshot.temp_alert_level,
            snapshot.solar_alert_level,
            snapshot.usb_power_alert_level
        );
    } else {
        ESP_LOGW(TAG, "MCP23017 snapshot unavailable");
    }
}

void board_init(void) {
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << PIN_STATUS_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    gpio_set_level(PIN_STATUS_LED, 1);

    board_configure_spi_idle_select(PIN_LORA_CS, "LORA_NSS");
    board_configure_spi_idle_select(PIN_IMU_CS, "IMU_CS");
    board_configure_spi_idle_select(PIN_FLASH_CS, "FLASH_CS");

    board_configure_optional_output(PIN_LORA_ENABLE, 1, "LORA_ENABLE");
    board_configure_optional_output(PIN_SENSORS_ENABLE, 1, "SENSORS_ENABLE");
    board_log_optional_hold("USBC_ENABLE", PIN_USBC_ENABLE);
    vTaskDelay(pdMS_TO_TICKS(250));
    board_configure_spi_idle_select(PIN_IMU_CS, "IMU_CS post-rail");
    board_configure_optional_output(PIN_IMU_RESET, 1, "IMU_RESET");
    board_configure_optional_output(PIN_IMU_WAKE, 1, "IMU_WAKE");
    if (PIN_GNSS_EN >= 0) {
        board_configure_optional_output(PIN_GNSS_EN, 1, "GNSS_EN");
    }
    if (PIN_GNSS_RESET >= 0) {
        board_configure_optional_output(PIN_GNSS_RESET, 1, "GNSS_RESET");
    }

    board_alert_snapshot_from_ports(0x00, 0x00, false);
    esp_err_t io_expander_result = board_init_io_expander();
    if (io_expander_result == ESP_OK) {
        ESP_LOGI(TAG, "MCP23017 initialized on I2C address 0x%02X", s_mcp23017_addr);
    } else {
        ESP_LOGW(TAG, "MCP23017 init skipped: %s", esp_err_to_name(io_expander_result));
    }

    ESP_LOGI(TAG, "Board initialized");
    board_log_config();
}
