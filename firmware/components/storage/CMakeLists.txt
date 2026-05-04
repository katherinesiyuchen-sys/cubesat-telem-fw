idf_component_register(
    SRCS
        "config_store.c"
        "log_store.c"
        "nvs_store.c"
    INCLUDE_DIRS "include"
    REQUIRES board nvs_flash esp_timer
)
