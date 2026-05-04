idf_component_register(
    SRCS "app_main.c" "system_init.c"
    INCLUDE_DIRS "."
    REQUIRES app board security storage tasks nvs_flash
)
