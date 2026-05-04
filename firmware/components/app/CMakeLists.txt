idf_component_register(
    SRCS
        "alert_logic.c"
        "app_state.c"
        "data_fusion.c"
        "event_router.c"
        "hardware_bringup.c"
        "self_test.c"
    INCLUDE_DIRS "include"
    REQUIRES board comms cubesat_hal drivers security storage
)
