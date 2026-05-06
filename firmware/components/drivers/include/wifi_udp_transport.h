#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    const char *ssid;
    const char *password;
    const char *ground_host;
    uint16_t local_port;
    uint16_t ground_port;
    uint32_t connect_timeout_ms;
} wifi_udp_transport_config_t;

esp_err_t wifi_udp_transport_init(const wifi_udp_transport_config_t *config);
esp_err_t wifi_udp_transport_send(const uint8_t *payload, size_t len, uint32_t timeout_ms);
esp_err_t wifi_udp_transport_receive(uint8_t *payload, size_t capacity, size_t *out_len, uint32_t timeout_ms);
bool wifi_udp_transport_is_ready(void);
