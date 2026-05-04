idf_component_register(
    SRCS
        "flash_store.c"
        "gnss.c"
        "imu.c"
        "lora.c"
        "power_monitor.c"
        "secure_element.c"
        "temp_sensor.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_uart esp_timer
)
