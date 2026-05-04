#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// ML-KEM-512 wire sizes from the NIST-standardized Kyber-derived KEM.
// This module is an integration boundary only until a real ML-KEM library is
// linked. Do not treat packets as lattice-secure while these APIs return
// ESP_ERR_NOT_SUPPORTED.
#define MLKEM512_PUBLIC_KEY_LEN     800
#define MLKEM512_SECRET_KEY_LEN     1632
#define MLKEM512_CIPHERTEXT_LEN     768
#define MLKEM_SHARED_SECRET_LEN     32

typedef struct {
    uint8_t shared_secret[MLKEM_SHARED_SECRET_LEN];
    bool established;
} mlkem_session_t;

esp_err_t mlkem_session_generate_keypair(
    uint8_t *public_key,
    size_t public_key_len,
    uint8_t *secret_key,
    size_t secret_key_len
);

esp_err_t mlkem_session_encapsulate(
    const uint8_t *peer_public_key,
    size_t peer_public_key_len,
    uint8_t *ciphertext,
    size_t ciphertext_len,
    mlkem_session_t *session
);

esp_err_t mlkem_session_decapsulate(
    const uint8_t *ciphertext,
    size_t ciphertext_len,
    const uint8_t *secret_key,
    size_t secret_key_len,
    mlkem_session_t *session
);
