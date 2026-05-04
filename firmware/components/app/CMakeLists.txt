idf_component_register(
    SRCS
        "alert_logic.c"
        "app_state.c"
        "data_fusion.c"
        "event_router.c"
    INCLUDE_DIRS "include"
    REQUIRES comms drivers security storage
)
