idf_component_register(
    SRCS "packet_codec.c" "packet.c" "telemetry_protocol.c" "transport.c"
    INCLUDE_DIRS "include"
    REQUIRES drivers
)
