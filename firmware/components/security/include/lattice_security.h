#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "command_protocol.h"
#include "esp_err.h"
#include "mldsa_auth.h"
#include "mlkem_session.h"
#include "packet.h"
#include "packet_crypto.h"

#define LATTICE_SECURITY_COMMAND_AUTH_KEY_ID 1
#define LATTICE_SECURITY_TRANSCRIPT_DIGEST_LEN 32
#define LATTICE_SECURITY_TRANSCRIPT_ROLE_NODE_IDENTITY 1
#define LATTICE_SECURITY_TRANSCRIPT_ROLE_GROUND_SESSION 2

typedef enum {
    LATTICE_SECURITY_UNINITIALIZED = 0,
    LATTICE_SECURITY_BACKEND_UNAVAILABLE,
    LATTICE_SECURITY_IDENTITY_READY,
    LATTICE_SECURITY_SESSION_READY,
} lattice_security_state_t;

esp_err_t lattice_security_init(void);
lattice_security_state_t lattice_security_state(void);
const char *lattice_security_state_name(lattice_security_state_t state);
bool lattice_security_backend_enabled(void);
bool lattice_security_has_packet_keys(void);

esp_err_t lattice_security_ensure_identity(void);

esp_err_t lattice_security_get_node_public_keys(
    uint8_t *mlkem_public_key,
    size_t mlkem_public_key_len,
    uint8_t *mldsa_public_key,
    size_t mldsa_public_key_len
);

esp_err_t lattice_security_accept_mlkem_ciphertext(
    const uint8_t *ciphertext,
    size_t ciphertext_len,
    uint32_t *out_session_id
);

esp_err_t lattice_security_sign(
    const uint8_t *message,
    size_t message_len,
    uint8_t *signature,
    size_t signature_len
);

esp_err_t lattice_security_verify(
    const uint8_t *message,
    size_t message_len,
    const uint8_t *public_key,
    size_t public_key_len,
    const uint8_t *signature,
    size_t signature_len
);

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
);

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
);

esp_err_t lattice_security_command_auth_tag(
    const hope_packet_t *packet,
    const command_request_t *request,
    uint8_t out_tag[COMMAND_AUTH_TAG_LEN]
);

esp_err_t lattice_security_verify_command(
    const hope_packet_t *packet,
    const command_request_t *request
);
