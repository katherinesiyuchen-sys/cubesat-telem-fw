idf_component_register(
    SRCS
        "flash_store.c"
        "gnss.c"
        "imu.c"
        "lora.c"
        "power_monitor.c"
        "secure_element.c"
        "temp_sensor.c"
        "wifi_udp_transport.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_uart esp_event esp_netif esp_timer esp_wifi lwip nvs_flash
)
