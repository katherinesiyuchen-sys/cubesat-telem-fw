#include "lattice_security.h"

#include <string.h>

#include "esp_check.h"
#include "key_store.h"
#include "psa/crypto.h"

#ifndef CONFIG_CUBESAT_USE_LIBOQS
#define CONFIG_CUBESAT_USE_LIBOQS 0
#endif

#ifndef CONFIG_CUBESAT_REQUIRE_COMMAND_AUTH
#define CONFIG_CUBESAT_REQUIRE_COMMAND_AUTH 0
#endif

static lattice_security_state_t s_state = LATTICE_SECURITY_UNINITIALIZED;
static key_store_material_t s_keys;
static packet_crypto_keys_t s_packet_keys;

static const uint8_t LATTICE_TRANSCRIPT_LABEL[] = "cubesat-lattice-transcript-v1";

static void write_u16_be(uint8_t *buf, uint16_t value) {
    buf[0] = (uint8_t)((value >> 8) & 0xFF);
    buf[1] = (uint8_t)(value & 0xFF);
}

static void write_u32_be(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)((value >> 24) & 0xFF);
    buf[1] = (uint8_t)((value >> 16) & 0xFF);
    buf[2] = (uint8_t)((value >> 8) & 0xFF);
    buf[3] = (uint8_t)(value & 0xFF);
}

static uint32_t read_u32_be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) |
        ((uint32_t)buf[1] << 16) |
        ((uint32_t)buf[2] << 8) |
        (uint32_t)buf[3];
}

static bool tag_equal(const uint8_t *left, const uint8_t *right, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= (uint8_t)(left[i] ^ right[i]);
    }
    return diff == 0;
}

static void packet_header_associated_data(const hope_packet_t *packet, uint8_t out[HOPE_PACKET_HEADER_LEN]) {
    out[0] = packet->version;
    out[1] = packet->type;
    write_u16_be(&out[2], packet->src_id);
    write_u16_be(&out[4], packet->dst_id);
    write_u32_be(&out[6], packet->session_id);
    write_u32_be(&out[10], packet->counter);
    write_u32_be(&out[14], packet->timestamp);
    write_u16_be(&out[18], (uint16_t)packet->payload_len);
}

static esp_err_t transcript_hash_update(psa_hash_operation_t *operation, const uint8_t *data, size_t len) {
    if (len == 0) {
        return ESP_OK;
    }
    if (operation == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return psa_hash_update(operation, data, len) == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
}

static esp_err_t lattice_security_transcript_digest(
    uint8_t transcript_role,
    uint16_t transfer_id,
    uint32_t session_id,
    const uint8_t *primary,
    size_t primary_len,
    const uint8_t *secondary,
    size_t secondary_len,
    uint8_t out_digest[LATTICE_SECURITY_TRANSCRIPT_DIGEST_LEN]
) {
    if (out_digest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((primary_len > 0 && primary == NULL) || (secondary_len > 0 && secondary == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (primary_len > UINT16_MAX || secondary_len > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    psa_status_t init_status = psa_crypto_init();
    if (init_status != PSA_SUCCESS) {
        return ESP_FAIL;
    }

    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    psa_status_t status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
    if (status != PSA_SUCCESS) {
        (void)psa_hash_abort(&operation);
        return ESP_FAIL;
    }

    uint8_t metadata[11];
    metadata[0] = transcript_role;
    write_u16_be(&metadata[1], transfer_id);
    write_u32_be(&metadata[3], session_id);
    write_u16_be(&metadata[7], (uint16_t)primary_len);
    write_u16_be(&metadata[9], (uint16_t)secondary_len);

    esp_err_t err = transcript_hash_update(&operation, LATTICE_TRANSCRIPT_LABEL, sizeof(LATTICE_TRANSCRIPT_LABEL) - 1U);
    if (err == ESP_OK) {
        err = transcript_hash_update(&operation, metadata, sizeof(metadata));
    }
    if (err == ESP_OK) {
        err = transcript_hash_update(&operation, primary, primary_len);
    }
    if (err == ESP_OK) {
        err = transcript_hash_update(&operation, secondary, secondary_len);
    }
    if (err != ESP_OK) {
        (void)psa_hash_abort(&operation);
        return err;
    }

    size_t digest_len = 0;
    status = psa_hash_finish(
        &operation,
        out_digest,
        LATTICE_SECURITY_TRANSCRIPT_DIGEST_LEN,
        &digest_len
    );
    if (status != PSA_SUCCESS || digest_len != LATTICE_SECURITY_TRANSCRIPT_DIGEST_LEN) {
        (void)psa_hash_abort(&operation);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t lattice_security_init(void) {
    memset(&s_packet_keys, 0, sizeof(s_packet_keys));
    memset(&s_keys, 0, sizeof(s_keys));

    esp_err_t err = key_store_init();
    if (err != ESP_OK) {
        s_state = LATTICE_SECURITY_UNINITIALIZED;
        return err;
    }

#if CONFIG_CUBESAT_USE_LIBOQS
    s_state = LATTICE_SECURITY_UNINITIALIZED;
    return lattice_security_ensure_identity();
#else
    s_state = LATTICE_SECURITY_BACKEND_UNAVAILABLE;
    return ESP_OK;
#endif
}

lattice_security_state_t lattice_security_state(void) {
    return s_state;
}

const char *lattice_security_state_name(lattice_security_state_t state) {
    switch (state) {
        case LATTICE_SECURITY_UNINITIALIZED:
            return "uninitialized";
        case LATTICE_SECURITY_BACKEND_UNAVAILABLE:
            return "backend-unavailable";
        case LATTICE_SECURITY_IDENTITY_READY:
            return "identity-ready";
        case LATTICE_SECURITY_SESSION_READY:
            return "session-ready";
        default:
            return "unknown";
    }
}

bool lattice_security_backend_enabled(void) {
#if CONFIG_CUBESAT_USE_LIBOQS
    return true;
#else
    return false;
#endif
}

bool lattice_security_has_packet_keys(void) {
    return s_packet_keys.valid;
}

esp_err_t lattice_security_ensure_identity(void) {
#if CONFIG_CUBESAT_USE_LIBOQS
    esp_err_t load_result = key_store_load(&s_keys);
    if (load_result == ESP_OK && s_keys.provisioned) {
        s_state = LATTICE_SECURITY_IDENTITY_READY;
        return ESP_OK;
    }

    memset(&s_keys, 0, sizeof(s_keys));
    ESP_RETURN_ON_ERROR(
        mlkem_session_generate_keypair(
            s_keys.mlkem_public_key,
            sizeof(s_keys.mlkem_public_key),
            s_keys.mlkem_secret_key,
            sizeof(s_keys.mlkem_secret_key)
        ),
        "lattice_security",
        "ML-KEM key generation failed"
    );
    ESP_RETURN_ON_ERROR(
        mldsa_auth_generate_keypair(
            s_keys.mldsa_public_key,
            sizeof(s_keys.mldsa_public_key),
            s_keys.mldsa_secret_key,
            sizeof(s_keys.mldsa_secret_key)
        ),
        "lattice_security",
        "ML-DSA key generation failed"
    );

    s_keys.provisioned = true;
    ESP_RETURN_ON_ERROR(key_store_save(&s_keys), "lattice_security", "key store save failed");
    s_state = LATTICE_SECURITY_IDENTITY_READY;
    return ESP_OK;
#else
    s_state = LATTICE_SECURITY_BACKEND_UNAVAILABLE;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t lattice_security_get_node_public_keys(
    uint8_t *mlkem_public_key,
    size_t mlkem_public_key_len,
    uint8_t *mldsa_public_key,
    size_t mldsa_public_key_len
) {
    if (mlkem_public_key == NULL || mldsa_public_key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mlkem_public_key_len != MLKEM512_PUBLIC_KEY_LEN || mldsa_public_key_len != MLDSA44_PUBLIC_KEY_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_RETURN_ON_ERROR(lattice_security_ensure_identity(), "lattice_security", "identity unavailable");
    memcpy(mlkem_public_key, s_keys.mlkem_public_key, mlkem_public_key_len);
    memcpy(mldsa_public_key, s_keys.mldsa_public_key, mldsa_public_key_len);
    return ESP_OK;
}

esp_err_t lattice_security_accept_mlkem_ciphertext(
    const uint8_t *ciphertext,
    size_t ciphertext_len,
    uint32_t *out_session_id
) {
    if (ciphertext == NULL || out_session_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(lattice_security_ensure_identity(), "lattice_security", "identity unavailable");

    mlkem_session_t session;
    ESP_RETURN_ON_ERROR(
        mlkem_session_decapsulate(
            ciphertext,
            ciphertext_len,
            s_keys.mlkem_secret_key,
            sizeof(s_keys.mlkem_secret_key),
            &session
        ),
        "lattice_security",
        "ML-KEM decapsulation failed"
    );
    ESP_RETURN_ON_ERROR(packet_crypto_derive_keys(&session, &s_packet_keys), "lattice_security", "packet key derivation failed");

    uint32_t session_id = read_u32_be(session.shared_secret) ^ 0xC0BE5A7U;
    if (session_id == 0) {
        session_id = 0xC0BE5A7U;
    }

    memset(&session, 0, sizeof(session));
    *out_session_id = session_id;
    s_state = LATTICE_SECURITY_SESSION_READY;
    return ESP_OK;
}

esp_err_t lattice_security_sign(
    const uint8_t *message,
    size_t message_len,
    uint8_t *signature,
    size_t signature_len
) {
    ESP_RETURN_ON_ERROR(lattice_security_ensure_identity(), "lattice_security", "identity unavailable");
    return mldsa_auth_sign(
        message,
        message_len,
        s_keys.mldsa_secret_key,
        sizeof(s_keys.mldsa_secret_key),
        signature,
        signature_len
    );
}

esp_err_t lattice_security_verify(
    const uint8_t *message,
    size_t message_len,
    const uint8_t *public_key,
    size_t public_key_len,
    const uint8_t *signature,
    size_t signature_len
) {
    return mldsa_auth_verify(message, message_len, public_key, public_key_len, signature, signature_len);
}

esp_err_t lattice_security_sign_handshake(
    uint8_t transcript_role,
    uint16_t transfer_id,
    uint32_t session_id,
    const uint8_t *primary,
    size_t primary_len,
    const uint8_t *secondary,
    size_t secondary_len,
    uint8_t *signature,
    size_t signature_len
) {
    if (signature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t digest[LATTICE_SECURITY_TRANSCRIPT_DIGEST_LEN];
    esp_err_t err = lattice_security_transcript_digest(
        transcript_role,
        transfer_id,
        session_id,
        primary,
        primary_len,
        secondary,
        secondary_len,
        digest
    );
    if (err != ESP_OK) {
        return err;
    }

    err = lattice_security_sign(digest, sizeof(digest), signature, signature_len);
    memset(digest, 0, sizeof(digest));
    return err;
}

esp_err_t lattice_security_verify_handshake(
    uint8_t transcript_role,
    uint16_t transfer_id,
    uint32_t session_id,
    const uint8_t *primary,
    size_t primary_len,
    const uint8_t *secondary,
    size_t secondary_len,
    const uint8_t *public_key,
    size_t public_key_len,
    const uint8_t *signature,
    size_t signature_len
) {
    uint8_t digest[LATTICE_SECURITY_TRANSCRIPT_DIGEST_LEN];
    esp_err_t err = lattice_security_transcript_digest(
        transcript_role,
        transfer_id,
        session_id,
        primary,
        primary_len,
        secondary,
        secondary_len,
        digest
    );
    if (err != ESP_OK) {
        return err;
    }

    err = lattice_security_verify(
        digest,
        sizeof(digest),
        public_key,
        public_key_len,
        signature,
        signature_len
    );
    memset(digest, 0, sizeof(digest));
    return err;
}

esp_err_t lattice_security_command_auth_tag(
    const hope_packet_t *packet,
    const command_request_t *request,
    uint8_t out_tag[COMMAND_AUTH_TAG_LEN]
) {
    if (packet == NULL || request == NULL || out_tag == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_packet_keys.valid) {
        return ESP_ERR_INVALID_STATE;
    }

    command_request_t canonical_request = *request;
    canonical_request.flags |= COMMAND_FLAG_AUTH_PRESENT;
    canonical_request.auth_key_id = LATTICE_SECURITY_COMMAND_AUTH_KEY_ID;
    memset(canonical_request.auth_tag, 0, sizeof(canonical_request.auth_tag));

    uint8_t payload[HOPE_MAX_PAYLOAD_LEN];
    size_t payload_len = 0;
    ESP_RETURN_ON_ERROR(
        command_protocol_build_request_payload(&canonical_request, payload, sizeof(payload), &payload_len),
        "lattice_security",
        "canonical command payload failed"
    );

    uint8_t associated_data[HOPE_PACKET_HEADER_LEN];
    hope_packet_t canonical_packet = *packet;
    canonical_packet.payload_len = payload_len;
    packet_header_associated_data(&canonical_packet, associated_data);

    uint8_t full_tag[PACKET_CRYPTO_TAG_LEN];
    ESP_RETURN_ON_ERROR(
        packet_crypto_auth_tag(
            s_packet_keys.rx_key,
            sizeof(s_packet_keys.rx_key),
            associated_data,
            sizeof(associated_data),
            payload,
            payload_len,
            full_tag,
            sizeof(full_tag)
        ),
        "lattice_security",
        "command tag failed"
    );

    memcpy(out_tag, full_tag, COMMAND_AUTH_TAG_LEN);
    memset(full_tag, 0, sizeof(full_tag));
    return ESP_OK;
}

esp_err_t lattice_security_verify_command(
    const hope_packet_t *packet,
    const command_request_t *request
) {
    if (packet == NULL || request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool auth_present = (request->flags & COMMAND_FLAG_AUTH_PRESENT) != 0;
    if (!auth_present) {
#if CONFIG_CUBESAT_REQUIRE_COMMAND_AUTH
        return ESP_ERR_INVALID_STATE;
#else
        return ESP_OK;
#endif
    }

    if (request->auth_key_id != LATTICE_SECURITY_COMMAND_AUTH_KEY_ID) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t expected[COMMAND_AUTH_TAG_LEN];
    esp_err_t err = lattice_security_command_auth_tag(packet, request, expected);
    if (err != ESP_OK) {
        return err;
    }

    bool ok = tag_equal(expected, request->auth_tag, COMMAND_AUTH_TAG_LEN);
    memset(expected, 0, sizeof(expected));
    return ok ? ESP_OK : ESP_ERR_INVALID_CRC;
}
