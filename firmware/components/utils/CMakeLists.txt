idf_component_register(
    SRCS
        "crc.c"
        "debug_log.c"
        "ringbuf.c"
        "time_utils.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
)
