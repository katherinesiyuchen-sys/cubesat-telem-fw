#include "transport.h"

esp_err_t transport_init(transport_t *transport, transport_send_fn_t send, transport_receive_fn_t receive, void *ctx) {
    if (transport == NULL || send == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    transport->send = send;
    transport->receive = receive;
    transport->ctx = ctx;
    return ESP_OK;
}

esp_err_t transport_send(const transport_t *transport, const uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (transport == NULL || transport->send == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return transport->send(data, len, timeout_ms, transport->ctx);
}

esp_err_t transport_receive(const transport_t *transport, uint8_t *data, size_t capacity, size_t *out_len, uint32_t timeout_ms) {
    if (transport == NULL || transport->receive == NULL || data == NULL || capacity == 0 || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return transport->receive(data, capacity, out_len, timeout_ms, transport->ctx);
}
