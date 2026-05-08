#include "loraq.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SX126X_CMD_SET_STANDBY              0x80
#define SX126X_CMD_SET_PACKET_TYPE          0x8A
#define SX126X_CMD_SET_RF_FREQUENCY         0x86
#define SX126X_CMD_SET_REGULATOR_MODE       0x96
#define SX126X_CMD_SET_PA_CONFIG            0x95
#define SX126X_CMD_SET_TX_PARAMS            0x8E
#define SX126X_CMD_SET_BUFFER_BASE_ADDRESS  0x8F
#define SX126X_CMD_SET_MODULATION_PARAMS    0x8B
#define SX126X_CMD_SET_PACKET_PARAMS        0x8C
#define SX126X_CMD_SET_DIO2_AS_RF_SWITCH    0x9D
#define SX126X_CMD_SET_DIO_IRQ_PARAMS       0x08
#define SX126X_CMD_CLEAR_IRQ_STATUS         0x02
#define SX126X_CMD_GET_IRQ_STATUS           0x12
#define SX126X_CMD_GET_RX_BUFFER_STATUS     0x13
#define SX126X_CMD_WRITE_BUFFER             0x0E
#define SX126X_CMD_READ_BUFFER              0x1E
#define SX126X_CMD_SET_TX                   0x83
#define SX126X_CMD_SET_RX                   0x82

#define SX126X_PACKET_TYPE_LORA             0x01
#define SX126X_STANDBY_RC                   0x00
#define SX126X_IRQ_TX_DONE                  0x0001
#define SX126X_IRQ_RX_DONE                  0x0002
#define SX126X_IRQ_HEADER_ERROR             0x0010
#define SX126X_IRQ_CRC_ERROR                0x0040
#define SX126X_IRQ_TIMEOUT                  0x0200
#define SX126X_IRQ_RADIO_EVENTS             (SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE | SX126X_IRQ_HEADER_ERROR | SX126X_IRQ_CRC_ERROR | SX126X_IRQ_TIMEOUT)
#define SX126X_TIMEOUT_DISABLED             0x000000UL
#define SX126X_TIMEOUT_MAX_FINITE           0xFFFFFEUL

static spi_device_handle_t s_spi = NULL;
static lora_config_t s_config;
static const TickType_t LORA_POLL_DELAY_TICKS = pdMS_TO_TICKS(10);
static int s_last_busy_level = -1;
static int s_last_dio1_level = -1;
static int s_last_reset_level = -1;
static int s_last_cs_level = -1;

static esp_err_t lora_configure_reset_pin(gpio_mode_t mode) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << s_config.pin_reset),
        .mode = mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static void lora_hard_reset(void) {
    ESP_ERROR_CHECK(lora_configure_reset_pin(GPIO_MODE_OUTPUT_OD));
    gpio_set_level(s_config.pin_reset, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(s_config.pin_reset, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(lora_configure_reset_pin(GPIO_MODE_INPUT));
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void lora_log_signal_levels(const char *context) {
    ESP_LOGW(
        "lora",
        "%s pin levels busy=%d dio1=%d reset=%d nss=%d",
        context,
        gpio_get_level(s_config.pin_busy),
        gpio_get_level(s_config.pin_dio1),
        gpio_get_level(s_config.pin_reset),
        gpio_get_level(s_config.pin_cs)
    );
}

void lora_log_debug_snapshot(const char *context) {
    if (s_spi == NULL) {
        return;
    }

    int busy = gpio_get_level(s_config.pin_busy);
    int dio1 = gpio_get_level(s_config.pin_dio1);
    int reset = gpio_get_level(s_config.pin_reset);
    int cs = gpio_get_level(s_config.pin_cs);

    ESP_LOGI(
        "lora",
        "%s signals busy=%d dio1=%d reset=%d nss=%d",
        context != NULL ? context : "LoRa",
        busy,
        dio1,
        reset,
        cs
    );

    s_last_busy_level = busy;
    s_last_dio1_level = dio1;
    s_last_reset_level = reset;
    s_last_cs_level = cs;
}

void lora_log_debug_changes(const char *context) {
    if (s_spi == NULL) {
        return;
    }

    int busy = gpio_get_level(s_config.pin_busy);
    int dio1 = gpio_get_level(s_config.pin_dio1);
    int reset = gpio_get_level(s_config.pin_reset);
    int cs = gpio_get_level(s_config.pin_cs);

    if (busy == s_last_busy_level &&
        dio1 == s_last_dio1_level &&
        reset == s_last_reset_level &&
        cs == s_last_cs_level) {
        return;
    }

    ESP_LOGI(
        "lora",
        "%s signal change busy=%d->%d dio1=%d->%d reset=%d->%d nss=%d->%d",
        context != NULL ? context : "LoRa",
        s_last_busy_level,
        busy,
        s_last_dio1_level,
        dio1,
        s_last_reset_level,
        reset,
        s_last_cs_level,
        cs
    );

    s_last_busy_level = busy;
    s_last_dio1_level = dio1;
    s_last_reset_level = reset;
    s_last_cs_level = cs;
}

static esp_err_t wait_while_busy(uint32_t timeout_ms) {
    int64_t deadline = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);

    while (gpio_get_level(s_config.pin_busy) != 0) {
        if (esp_timer_get_time() > deadline) {
            lora_log_signal_levels("SX1262 busy timeout before recovery");
            lora_hard_reset();
            lora_log_signal_levels("SX1262 after busy recovery reset");

            int64_t recovery_deadline = esp_timer_get_time() + (100 * 1000);
            while (gpio_get_level(s_config.pin_busy) != 0) {
                if (esp_timer_get_time() > recovery_deadline) {
                    lora_log_signal_levels("SX1262 busy still high after recovery");
                    return ESP_ERR_TIMEOUT;
                }
                vTaskDelay(LORA_POLL_DELAY_TICKS);
            }
            return ESP_OK;
        }
        vTaskDelay(LORA_POLL_DELAY_TICKS);
    }

    return ESP_OK;
}

static esp_err_t write_command(uint8_t command, const uint8_t *data, size_t len) {
    ESP_RETURN_ON_ERROR(wait_while_busy(1000), "lora", "SX1262 busy timeout");

    uint8_t tx[1 + 16];
    if (len > sizeof(tx) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    tx[0] = command;
    if (len > 0) {
        memcpy(&tx[1], data, len);
    }

    spi_transaction_t transaction = {
        .length = 8 * (1 + len),
        .tx_buffer = tx,
    };

    return spi_device_transmit(s_spi, &transaction);
}

static esp_err_t read_command(uint8_t command, uint8_t *out, size_t len) {
    if (out == NULL || len == 0 || len > 16) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(wait_while_busy(1000), "lora", "SX1262 busy timeout");

    uint8_t tx[2 + 16] = { 0 };
    uint8_t rx[2 + 16] = { 0 };
    tx[0] = command;
    tx[1] = 0x00;

    spi_transaction_t transaction = {
        .length = 8 * (2 + len),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    ESP_RETURN_ON_ERROR(spi_device_transmit(s_spi, &transaction), "lora", "read command failed");
    memcpy(out, &rx[2], len);
    return ESP_OK;
}

static esp_err_t wait_for_irq(uint16_t mask, uint32_t timeout_ms, uint16_t *out_irq, bool log_timeout) {
    int64_t deadline = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);

    while (esp_timer_get_time() <= deadline) {
        uint8_t irq_status[2] = { 0 };
        esp_err_t err = read_command(SX126X_CMD_GET_IRQ_STATUS, irq_status, sizeof(irq_status));
        if (err != ESP_OK) {
            lora_log_signal_levels("IRQ poll failed");
            return err;
        }

        uint16_t irq = ((uint16_t)irq_status[0] << 8) | irq_status[1];
        if ((irq & mask) != 0) {
            if (out_irq != NULL) {
                *out_irq = irq;
            }
            return ESP_OK;
        }

        vTaskDelay(LORA_POLL_DELAY_TICKS);
    }

    if (log_timeout) {
        lora_log_signal_levels("IRQ wait timeout");
    }
    return ESP_ERR_TIMEOUT;
}

static uint8_t bandwidth_to_reg(uint32_t bandwidth_hz) {
    if (bandwidth_hz <= 7800) {
        return 0x00;
    }
    if (bandwidth_hz <= 10400) {
        return 0x08;
    }
    if (bandwidth_hz <= 15600) {
        return 0x01;
    }
    if (bandwidth_hz <= 20800) {
        return 0x09;
    }
    if (bandwidth_hz <= 31250) {
        return 0x02;
    }
    if (bandwidth_hz <= 41700) {
        return 0x0A;
    }
    if (bandwidth_hz <= 62500) {
        return 0x03;
    }
    if (bandwidth_hz <= 125000) {
        return 0x04;
    }
    if (bandwidth_hz <= 250000) {
        return 0x05;
    }
    return 0x06;
}

static void timeout_ms_to_sx126x(uint32_t timeout_ms, uint8_t out[3]) {
    uint32_t timeout_units = (timeout_ms == 0) ? 1 : timeout_ms * 64UL;
    if (timeout_units > SX126X_TIMEOUT_MAX_FINITE) {
        timeout_units = SX126X_TIMEOUT_MAX_FINITE;
    }

    out[0] = (uint8_t)(timeout_units >> 16);
    out[1] = (uint8_t)(timeout_units >> 8);
    out[2] = (uint8_t)timeout_units;
}

static esp_err_t configure_radio(void) {
    uint8_t standby[] = { SX126X_STANDBY_RC };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby)), "lora", "standby failed");

    uint8_t packet_type[] = { SX126X_PACKET_TYPE_LORA };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_PACKET_TYPE, packet_type, sizeof(packet_type)), "lora", "packet type failed");

    uint8_t regulator_mode[] = { 0x00 };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_REGULATOR_MODE, regulator_mode, sizeof(regulator_mode)), "lora", "regulator mode failed");

    uint32_t rf_freq = (uint32_t)(((uint64_t)s_config.frequency_hz << 25) / 32000000ULL);
    uint8_t frequency[] = {
        (uint8_t)(rf_freq >> 24),
        (uint8_t)(rf_freq >> 16),
        (uint8_t)(rf_freq >> 8),
        (uint8_t)rf_freq,
    };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_RF_FREQUENCY, frequency, sizeof(frequency)), "lora", "frequency failed");

    uint8_t pa_config[] = { 0x04, 0x07, 0x00, 0x01 };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_PA_CONFIG, pa_config, sizeof(pa_config)), "lora", "pa config failed");

    uint8_t tx_params[] = { (uint8_t)s_config.tx_power_dbm, 0x04 };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_TX_PARAMS, tx_params, sizeof(tx_params)), "lora", "tx params failed");

    uint8_t dio2_rf_switch[] = { 0x01 };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_DIO2_AS_RF_SWITCH, dio2_rf_switch, sizeof(dio2_rf_switch)), "lora", "dio2 rf switch failed");

    uint8_t buffer_base[] = { 0x00, 0x00 };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_BUFFER_BASE_ADDRESS, buffer_base, sizeof(buffer_base)), "lora", "buffer base failed");

    uint8_t modulation[] = {
        s_config.spreading_factor,
        bandwidth_to_reg(s_config.bandwidth_hz),
        s_config.coding_rate,
        0x00,
    };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_MODULATION_PARAMS, modulation, sizeof(modulation)), "lora", "modulation failed");

    uint8_t irq[] = {
        (uint8_t)(SX126X_IRQ_RADIO_EVENTS >> 8),
        (uint8_t)SX126X_IRQ_RADIO_EVENTS,
        (uint8_t)(SX126X_IRQ_RADIO_EVENTS >> 8),
        (uint8_t)SX126X_IRQ_RADIO_EVENTS,
        0x00,
        0x00,
        0x00,
        0x00,
    };
    return write_command(SX126X_CMD_SET_DIO_IRQ_PARAMS, irq, sizeof(irq));
}

esp_err_t lora_init(const lora_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;

    ESP_RETURN_ON_ERROR(lora_configure_reset_pin(GPIO_MODE_INPUT), "lora", "reset gpio failed");

    gpio_config_t input_cfg = {
        .pin_bit_mask = (1ULL << s_config.pin_busy) | (1ULL << s_config.pin_dio1),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input_cfg), "lora", "input gpio failed");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = s_config.pin_mosi,
        .miso_io_num = s_config.pin_miso,
        .sclk_io_num = s_config.pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 512,
    };
    esp_err_t bus_result = spi_bus_initialize(s_config.spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (bus_result != ESP_OK && bus_result != ESP_ERR_INVALID_STATE) {
        return bus_result;
    }

    if (s_spi == NULL) {
        spi_device_interface_config_t dev_cfg = {
            .clock_speed_hz = 8000000,
            .mode = 0,
            .spics_io_num = s_config.pin_cs,
            .queue_size = 1,
        };
        ESP_RETURN_ON_ERROR(spi_bus_add_device(s_config.spi_host, &dev_cfg, &s_spi), "lora", "add spi device failed");
    }

    lora_hard_reset();
    lora_log_signal_levels("After hard reset");

    esp_err_t err = configure_radio();
    if (err == ESP_OK) {
        lora_log_debug_snapshot("LoRa initialized");
    }
    return err;
}

esp_err_t lora_send(const uint8_t *payload, size_t len, uint32_t timeout_ms) {
    if (payload == NULL || len == 0 || len > 255 || s_spi == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t standby[] = { SX126X_STANDBY_RC };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby)), "lora", "standby before tx failed");

    uint8_t clear_irq[] = {
        (uint8_t)(SX126X_IRQ_RADIO_EVENTS >> 8),
        (uint8_t)SX126X_IRQ_RADIO_EVENTS,
    };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_CLEAR_IRQ_STATUS, clear_irq, sizeof(clear_irq)), "lora", "clear irq failed");

    uint8_t packet_params[] = {
        0x00,
        0x0C,
        0x00,
        (uint8_t)len,
        0x01,
        0x00,
    };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_PACKET_PARAMS, packet_params, sizeof(packet_params)), "lora", "packet params failed");

    uint8_t tx[1 + 1 + 255];
    tx[0] = SX126X_CMD_WRITE_BUFFER;
    tx[1] = 0x00;
    memcpy(&tx[2], payload, len);

    ESP_RETURN_ON_ERROR(wait_while_busy(1000), "lora", "busy before write");
    spi_transaction_t transaction = {
        .length = 8 * (2 + len),
        .tx_buffer = tx,
    };
    ESP_RETURN_ON_ERROR(spi_device_transmit(s_spi, &transaction), "lora", "write buffer failed");

    uint8_t tx_timeout[] = {
        (uint8_t)(SX126X_TIMEOUT_DISABLED >> 16),
        (uint8_t)(SX126X_TIMEOUT_DISABLED >> 8),
        (uint8_t)SX126X_TIMEOUT_DISABLED,
    };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_TX, tx_timeout, sizeof(tx_timeout)), "lora", "set tx failed");

    uint16_t irq = 0;
    esp_err_t wait_result = wait_for_irq((uint16_t)(SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT), timeout_ms, &irq, true);
    if (wait_result != ESP_OK) {
        (void)write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby));
        (void)write_command(SX126X_CMD_CLEAR_IRQ_STATUS, clear_irq, sizeof(clear_irq));
        return wait_result;
    }
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_CLEAR_IRQ_STATUS, clear_irq, sizeof(clear_irq)), "lora", "clear irq after tx failed");

    if ((irq & SX126X_IRQ_TIMEOUT) != 0) {
        (void)configure_radio();
        return ESP_ERR_TIMEOUT;
    }
    if ((irq & SX126X_IRQ_TX_DONE) == 0) {
        (void)configure_radio();
        return ESP_FAIL;
    }

    (void)write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby));
    return ESP_OK;
}

esp_err_t lora_receive(uint8_t *payload, size_t capacity, size_t *out_len, uint32_t timeout_ms) {
    if (payload == NULL || out_len == NULL || capacity == 0 || capacity > 255 || s_spi == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_len = 0;

    uint8_t clear_irq[] = {
        (uint8_t)(SX126X_IRQ_RADIO_EVENTS >> 8),
        (uint8_t)SX126X_IRQ_RADIO_EVENTS,
    };
    uint8_t standby[] = { SX126X_STANDBY_RC };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby)), "lora", "standby before rx failed");
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_CLEAR_IRQ_STATUS, clear_irq, sizeof(clear_irq)), "lora", "clear irq before rx failed");

    uint8_t packet_params[] = {
        0x00,
        0x0C,
        0x00,
        0xFF,
        0x01,
        0x00,
    };
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_PACKET_PARAMS, packet_params, sizeof(packet_params)), "lora", "rx packet params failed");

    uint8_t rx_timeout[3] = { 0 };
    timeout_ms_to_sx126x(timeout_ms, rx_timeout);
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_RX, rx_timeout, sizeof(rx_timeout)), "lora", "set rx failed");

    uint16_t irq = 0;
    esp_err_t wait_result = wait_for_irq(
        (uint16_t)(SX126X_IRQ_RX_DONE | SX126X_IRQ_HEADER_ERROR | SX126X_IRQ_CRC_ERROR | SX126X_IRQ_TIMEOUT),
        timeout_ms + 50U,
        &irq,
        false
    );
    if (wait_result != ESP_OK) {
        (void)write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby));
        (void)write_command(SX126X_CMD_CLEAR_IRQ_STATUS, clear_irq, sizeof(clear_irq));
        (void)configure_radio();
        return wait_result;
    }
    ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_CLEAR_IRQ_STATUS, clear_irq, sizeof(clear_irq)), "lora", "clear irq after rx failed");

    if ((irq & SX126X_IRQ_TIMEOUT) != 0) {
        (void)write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby));
        return ESP_ERR_TIMEOUT;
    }
    if ((irq & (SX126X_IRQ_HEADER_ERROR | SX126X_IRQ_CRC_ERROR)) != 0) {
        (void)write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby));
        return ESP_ERR_INVALID_CRC;
    }
    if ((irq & SX126X_IRQ_RX_DONE) == 0) {
        (void)write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby));
        return ESP_FAIL;
    }

    uint8_t rx_status[2] = { 0 };
    ESP_RETURN_ON_ERROR(read_command(SX126X_CMD_GET_RX_BUFFER_STATUS, rx_status, sizeof(rx_status)), "lora", "rx buffer status failed");
    uint8_t payload_len = rx_status[0];
    uint8_t start_pointer = rx_status[1];

    if (payload_len == 0) {
        (void)write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby));
        return ESP_ERR_NOT_FOUND;
    }
    if (payload_len > capacity) {
        (void)write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby));
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t tx[3 + 255] = { 0 };
    uint8_t rx[3 + 255] = { 0 };
    tx[0] = SX126X_CMD_READ_BUFFER;
    tx[1] = start_pointer;
    tx[2] = 0x00;

    ESP_RETURN_ON_ERROR(wait_while_busy(1000), "lora", "busy before read buffer");
    spi_transaction_t transaction = {
        .length = 8 * (3 + payload_len),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    ESP_RETURN_ON_ERROR(spi_device_transmit(s_spi, &transaction), "lora", "read buffer failed");

    memcpy(payload, &rx[3], payload_len);
    *out_len = payload_len;
    (void)write_command(SX126X_CMD_SET_STANDBY, standby, sizeof(standby));
    return ESP_OK;
}
