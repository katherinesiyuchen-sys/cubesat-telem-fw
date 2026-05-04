#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// ML-DSA-44 wire sizes from the NIST-standardized Dilithium-derived signature
// scheme. This is an integration boundary only until a real ML-DSA library is
// linked.
#define MLDSA44_PUBLIC_KEY_LEN      1312
#define MLDSA44_SECRET_KEY_LEN      2560
#define MLDSA44_SIGNATURE_LEN       2420

esp_err_t mldsa_auth_generate_keypair(
    uint8_t *public_key,
    size_t public_key_len,
    uint8_t *secret_key,
    size_t secret_key_len
);

esp_err_t mldsa_auth_sign(
    const uint8_t *message,
    size_t message_len,
    const uint8_t *secret_key,
    size_t secret_key_len,
    uint8_t *signature,
    size_t signature_len
);

esp_err_t mldsa_auth_verify(
    const uint8_t *message,
    size_t message_len,
    const uint8_t *public_key,
    size_t public_key_len,
    const uint8_t *signature,
    size_t signature_len
);
