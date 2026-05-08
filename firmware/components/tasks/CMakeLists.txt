idf_component_register(
    SRCS
        "gnss_task.c"
        "health_task.c"
        "logger_task.c"
        "lora_task.c"
        "sensor_task.c"
    INCLUDE_DIRS "include"
    REQUIRES app board comms cubesat_hal drivers security storage utils esp_timer freertos
)
