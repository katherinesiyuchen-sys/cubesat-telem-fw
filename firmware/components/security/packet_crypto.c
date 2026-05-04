#include "packet_crypto.h"

#include <string.h>

#include "esp_check.h"
#include "psa/crypto.h"

static esp_err_t hmac_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *input_a,
    size_t input_a_len,
    const uint8_t *input_b,
    size_t input_b_len,
    uint8_t *out,
    size_t out_len
) {
    if (
        key == NULL ||
        out == NULL ||
        out_len != PACKET_CRYPTO_TAG_LEN ||
        (input_a_len > 0 && input_a == NULL) ||
        (input_b_len > 0 && input_b == NULL)
    ) {
        return ESP_ERR_INVALID_ARG;
    }

    if (psa_crypto_init() != PSA_SUCCESS) {
        return ESP_FAIL;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = 0;
    psa_mac_operation_t operation = PSA_MAC_OPERATION_INIT;
    size_t mac_len = 0;

    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, key_len * 8U);

    psa_status_t status = psa_import_key(&attributes, key, key_len, &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        return ESP_FAIL;
    }

    status = psa_mac_sign_setup(&operation, key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    if (status == PSA_SUCCESS && input_a_len > 0) {
        status = psa_mac_update(&operation, input_a, input_a_len);
    }
    if (status == PSA_SUCCESS && input_b_len > 0) {
        status = psa_mac_update(&operation, input_b, input_b_len);
    }
    if (status == PSA_SUCCESS) {
        status = psa_mac_sign_finish(&operation, out, out_len, &mac_len);
    }

    psa_mac_abort(&operation);
    psa_destroy_key(key_id);

    return (status == PSA_SUCCESS && mac_len == out_len) ? ESP_OK : ESP_FAIL;
}

esp_err_t packet_crypto_derive_keys(
    const mlkem_session_t *session,
    packet_crypto_keys_t *keys
) {
    if (session == NULL || keys == NULL || !session->established) {
        return ESP_ERR_INVALID_ARG;
    }

    static const uint8_t tx_label[] = "CubeSat LoRa TX key v1";
    static const uint8_t rx_label[] = "CubeSat LoRa RX key v1";

    memset(keys, 0, sizeof(*keys));

    ESP_RETURN_ON_ERROR(
        hmac_sha256(
            session->shared_secret,
            sizeof(session->shared_secret),
            tx_label,
            sizeof(tx_label) - 1,
            NULL,
            0,
            keys->tx_key,
            sizeof(keys->tx_key)
        ),
        "packet_crypto",
        "tx key derivation failed"
    );

    ESP_RETURN_ON_ERROR(
        hmac_sha256(
            session->shared_secret,
            sizeof(session->shared_secret),
            rx_label,
            sizeof(rx_label) - 1,
            NULL,
            0,
            keys->rx_key,
            sizeof(keys->rx_key)
        ),
        "packet_crypto",
        "rx key derivation failed"
    );

    keys->valid = true;
    return ESP_OK;
}

esp_err_t packet_crypto_auth_tag(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *associated_data,
    size_t associated_data_len,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *tag,
    size_t tag_len
) {
    if (key == NULL || key_len != PACKET_CRYPTO_KEY_LEN || tag == NULL || tag_len != PACKET_CRYPTO_TAG_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    return hmac_sha256(
        key,
        key_len,
        associated_data,
        associated_data_len,
        payload,
        payload_len,
        tag,
        tag_len
    );
}
