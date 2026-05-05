set(security_requires "")

list(APPEND security_requires comms mbedtls nvs_flash storage)

if(CONFIG_CUBESAT_USE_LIBOQS)
    list(APPEND security_requires oqs)
endif()

idf_component_register(
    SRCS
        "session.c"
        "replay.c"
        "mlkem_session.c"
        "mldsa_auth.c"
        "key_store.c"
        "lattice_security.c"
        "packet_crypto.c"
    INCLUDE_DIRS "include"
    REQUIRES ${security_requires}
)
