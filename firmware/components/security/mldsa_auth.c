#include "mldsa_auth.h"

#include <string.h>

#ifndef CONFIG_CUBESAT_USE_LIBOQS
#define CONFIG_CUBESAT_USE_LIBOQS 0
#endif

#if CONFIG_CUBESAT_USE_LIBOQS
#include <oqs/oqs.h>
#endif

// ML-DSA is used for full-size handshake signatures when liboqs is available.
// Compact command authentication tags live in lattice_security.c and are HMACs
// derived from the ML-KEM session secret.
esp_err_t mldsa_auth_generate_keypair(
    uint8_t *public_key,
    size_t public_key_len,
    uint8_t *secret_key,
    size_t secret_key_len
) {
    if (public_key == NULL || secret_key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (public_key_len != MLDSA44_PUBLIC_KEY_LEN || secret_key_len != MLDSA44_SECRET_KEY_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

#if CONFIG_CUBESAT_USE_LIBOQS
    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
    if (sig == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (sig->length_public_key != public_key_len || sig->length_secret_key != secret_key_len) {
        OQS_SIG_free(sig);
        return ESP_ERR_INVALID_SIZE;
    }

    OQS_STATUS status = OQS_SIG_keypair(sig, public_key, secret_key);
    OQS_SIG_free(sig);
    return status == OQS_SUCCESS ? ESP_OK : ESP_FAIL;
#else
    memset(public_key, 0, public_key_len);
    memset(secret_key, 0, secret_key_len);
    return ESP_ERR_NOT_SUPPORTED;
#endif
}


// Signs the given message with the provided secret key and writes a full
// ML-DSA-44 signature to the output buffer.
esp_err_t mldsa_auth_sign(
    const uint8_t *message,
    size_t message_len,
    const uint8_t *secret_key,
    size_t secret_key_len,
    uint8_t *signature,
    size_t signature_len
) {
    if (message == NULL || message_len == 0 || secret_key == NULL || signature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (secret_key_len != MLDSA44_SECRET_KEY_LEN || signature_len != MLDSA44_SIGNATURE_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

#if CONFIG_CUBESAT_USE_LIBOQS
    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
    if (sig == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (sig->length_secret_key != secret_key_len || sig->length_signature != signature_len) {
        OQS_SIG_free(sig);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t actual_signature_len = 0;
    OQS_STATUS status = OQS_SIG_sign(
        sig,
        signature,
        &actual_signature_len,
        message,
        message_len,
        secret_key
    );
    OQS_SIG_free(sig);

    if (status != OQS_SUCCESS || actual_signature_len != signature_len) {
        memset(signature, 0, signature_len);
        return ESP_FAIL;
    }

    return ESP_OK;
#else
    memset(signature, 0, signature_len);
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t mldsa_auth_verify(
    const uint8_t *message,
    size_t message_len,
    const uint8_t *public_key,
    size_t public_key_len,
    const uint8_t *signature,
    size_t signature_len
) {
    if (message == NULL || message_len == 0 || public_key == NULL || signature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (public_key_len != MLDSA44_PUBLIC_KEY_LEN || signature_len != MLDSA44_SIGNATURE_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

#if CONFIG_CUBESAT_USE_LIBOQS
    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
    if (sig == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (sig->length_public_key != public_key_len || sig->length_signature != signature_len) {
        OQS_SIG_free(sig);
        return ESP_ERR_INVALID_SIZE;
    }

    OQS_STATUS status = OQS_SIG_verify(sig, message, message_len, signature, signature_len, public_key);
    OQS_SIG_free(sig);
    return status == OQS_SUCCESS ? ESP_OK : ESP_ERR_INVALID_CRC;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
