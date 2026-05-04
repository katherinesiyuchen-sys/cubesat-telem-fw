#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "mldsa_auth.h"
#include "mlkem_session.h"

typedef struct {
    uint8_t mlkem_public_key[MLKEM512_PUBLIC_KEY_LEN];
    uint8_t mlkem_secret_key[MLKEM512_SECRET_KEY_LEN];
    uint8_t mldsa_public_key[MLDSA44_PUBLIC_KEY_LEN];
    uint8_t mldsa_secret_key[MLDSA44_SECRET_KEY_LEN];
    bool provisioned;
} key_store_material_t;

esp_err_t key_store_init(void);
esp_err_t key_store_load(key_store_material_t *keys);
esp_err_t key_store_save(const key_store_material_t *keys);
