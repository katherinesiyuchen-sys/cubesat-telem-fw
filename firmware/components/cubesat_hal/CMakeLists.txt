idf_component_register(
    SRCS
        "hal_adc.c"
        "hal_gpio.c"
        "hal_i2c.c"
        "hal_spi.c"
        "hal_uart.c"
        "hal_urat.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_uart esp_timer
)
