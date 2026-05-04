idf_component_register(
    SRCS "command_protocol.c" "diagnostic_protocol.c" "packet_codec.c" "packet.c" "telemetry_protocol.c" "transport.c"
    INCLUDE_DIRS "include"
    REQUIRES drivers
)
