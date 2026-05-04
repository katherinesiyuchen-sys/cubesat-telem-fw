#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef esp_err_t (*transport_send_fn_t)(const uint8_t *data, size_t len, uint32_t timeout_ms, void *ctx);
typedef esp_err_t (*transport_receive_fn_t)(uint8_t *data, size_t capacity, size_t *out_len, uint32_t timeout_ms, void *ctx);

typedef struct {
    transport_send_fn_t send;
    transport_receive_fn_t receive;
    void *ctx;
} transport_t;

esp_err_t transport_init(transport_t *transport, transport_send_fn_t send, transport_receive_fn_t receive, void *ctx);
esp_err_t transport_send(const transport_t *transport, const uint8_t *data, size_t len, uint32_t timeout_ms);
esp_err_t transport_receive(const transport_t *transport, uint8_t *data, size_t capacity, size_t *out_len, uint32_t timeout_ms);
