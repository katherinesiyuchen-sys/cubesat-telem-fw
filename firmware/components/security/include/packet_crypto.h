#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "mlkem_session.h"

#define PACKET_CRYPTO_KEY_LEN       32
#define PACKET_CRYPTO_TAG_LEN       32

typedef struct {
    uint8_t tx_key[PACKET_CRYPTO_KEY_LEN];
    uint8_t rx_key[PACKET_CRYPTO_KEY_LEN];
    bool valid;
} packet_crypto_keys_t;

esp_err_t packet_crypto_derive_keys(
    const mlkem_session_t *session,
    packet_crypto_keys_t *keys
);

esp_err_t packet_crypto_auth_tag(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *associated_data,
    size_t associated_data_len,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *tag,
    size_t tag_len
);
