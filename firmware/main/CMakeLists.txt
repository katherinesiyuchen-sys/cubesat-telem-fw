idf_component_register(
    SRCS "app_main.c" "system_init.c"
    INCLUDE_DIRS "."
    REQUIRES board tasks
)