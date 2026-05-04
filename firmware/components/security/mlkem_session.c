#include "mlkem_session.h"

#include <stdbool.h>
#include <string.h>

#include "esp_check.h"

#ifndef CONFIG_CUBESAT_USE_LIBOQS
#define CONFIG_CUBESAT_USE_LIBOQS 0
#endif

#if CONFIG_CUBESAT_USE_LIBOQS
#include <oqs/oqs.h>
#endif

static esp_err_t validate_buffer(const void *buffer, size_t actual_len, size_t expected_len) {
    if (buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (actual_len != expected_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t mlkem_session_generate_keypair(
    uint8_t *public_key,
    size_t public_key_len,
    uint8_t *secret_key,
    size_t secret_key_len
) {
    ESP_RETURN_ON_ERROR(
        validate_buffer(public_key, public_key_len, MLKEM512_PUBLIC_KEY_LEN),
        "mlkem",
        "invalid public key buffer"
    );
    ESP_RETURN_ON_ERROR(
        validate_buffer(secret_key, secret_key_len, MLKEM512_SECRET_KEY_LEN),
        "mlkem",
        "invalid secret key buffer"
    );

#if CONFIG_CUBESAT_USE_LIBOQS
    OQS_KEM *kem = OQS_KEM_new(OQS_KEM_alg_ml_kem_512);
    if (kem == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (kem->length_public_key != public_key_len || kem->length_secret_key != secret_key_len) {
        OQS_KEM_free(kem);
        return ESP_ERR_INVALID_SIZE;
    }

    OQS_STATUS status = OQS_KEM_keypair(kem, public_key, secret_key);
    OQS_KEM_free(kem);
    return status == OQS_SUCCESS ? ESP_OK : ESP_FAIL;
#else
    memset(public_key, 0, public_key_len);
    memset(secret_key, 0, secret_key_len);
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t mlkem_session_encapsulate(
    const uint8_t *peer_public_key,
    size_t peer_public_key_len,
    uint8_t *ciphertext,
    size_t ciphertext_len,
    mlkem_session_t *session
) {
    ESP_RETURN_ON_ERROR(
        validate_buffer(peer_public_key, peer_public_key_len, MLKEM512_PUBLIC_KEY_LEN),
        "mlkem",
        "invalid peer public key"
    );
    ESP_RETURN_ON_ERROR(
        validate_buffer(ciphertext, ciphertext_len, MLKEM512_CIPHERTEXT_LEN),
        "mlkem",
        "invalid ciphertext buffer"
    );
    if (session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_CUBESAT_USE_LIBOQS
    OQS_KEM *kem = OQS_KEM_new(OQS_KEM_alg_ml_kem_512);
    if (kem == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (
        kem->length_public_key != peer_public_key_len ||
        kem->length_ciphertext != ciphertext_len ||
        kem->length_shared_secret != MLKEM_SHARED_SECRET_LEN
    ) {
        OQS_KEM_free(kem);
        return ESP_ERR_INVALID_SIZE;
    }

    memset(session, 0, sizeof(*session));
    OQS_STATUS status = OQS_KEM_encaps(kem, ciphertext, session->shared_secret, peer_public_key);
    OQS_KEM_free(kem);

    if (status != OQS_SUCCESS) {
        memset(session, 0, sizeof(*session));
        return ESP_FAIL;
    }

    session->established = true;
    return ESP_OK;
#else
    memset(ciphertext, 0, ciphertext_len);
    memset(session, 0, sizeof(*session));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t mlkem_session_decapsulate(
    const uint8_t *ciphertext,
    size_t ciphertext_len,
    const uint8_t *secret_key,
    size_t secret_key_len,
    mlkem_session_t *session
) {
    ESP_RETURN_ON_ERROR(
        validate_buffer(ciphertext, ciphertext_len, MLKEM512_CIPHERTEXT_LEN),
        "mlkem",
        "invalid ciphertext"
    );
    ESP_RETURN_ON_ERROR(
        validate_buffer(secret_key, secret_key_len, MLKEM512_SECRET_KEY_LEN),
        "mlkem",
        "invalid secret key"
    );
    if (session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_CUBESAT_USE_LIBOQS
    OQS_KEM *kem = OQS_KEM_new(OQS_KEM_alg_ml_kem_512);
    if (kem == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (
        kem->length_ciphertext != ciphertext_len ||
        kem->length_secret_key != secret_key_len ||
        kem->length_shared_secret != MLKEM_SHARED_SECRET_LEN
    ) {
        OQS_KEM_free(kem);
        return ESP_ERR_INVALID_SIZE;
    }

    memset(session, 0, sizeof(*session));
    OQS_STATUS status = OQS_KEM_decaps(kem, session->shared_secret, ciphertext, secret_key);
    OQS_KEM_free(kem);

    if (status != OQS_SUCCESS) {
        memset(session, 0, sizeof(*session));
        return ESP_FAIL;
    }

    session->established = true;
    return ESP_OK;
#else
    memset(session, 0, sizeof(*session));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
