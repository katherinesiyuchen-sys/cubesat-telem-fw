idf_component_register(
    SRCS "board.c"
    INCLUDE_DIRS "include"
    REQUIRES cubesat_hal esp_driver_gpio
)
