from groundstation.backend.event_engine import EventEngine
from groundstation.backend.packet_parser import (
    HOPE_PACKET_TYPE_ACK,
    HOPE_PACKET_TYPE_DIAGNOSTIC,
    HOPE_PACKET_TYPE_HANDSHAKE,
    HOPE_PACKET_TYPE_TELEMETRY,
    decode_packet,
    encode_command_packet,
)
from groundstation.backend.rangepi import parse_rangepi_line
from groundstation.backend.serial_link import SerialLink
from groundstation.models.command import COMMAND_OPCODE_PING, COMMAND_OPCODE_ROTATE_SESSION, COMMAND_OPCODE_SELF_TEST, ack_status_name, parse_ack_payload
from groundstation.models.diagnostic import parse_diagnostic_payload
from groundstation.models.lattice import (
    LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE,
    LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY,
    LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY,
    parse_fragment_payload,
)
from groundstation.models.telemetry import parse_telemetry_payload


def _write_command(link: SerialLink, command_id: int, opcode: int) -> None:
    raw = encode_command_packet(command_id=command_id, opcode=opcode, counter=command_id)
    link.write_line(f"TX {raw.hex().upper()}")


def main() -> None:
    link = SerialLink("sim://cubesat", timeout=0.1)
    link.open()
    try:
        engine = EventEngine()

        _write_command(link, 40, COMMAND_OPCODE_PING)
        ack_line = link.read_line()
        telemetry_line = link.read_line()
        assert ack_line is not None
        assert telemetry_line is not None

        ack_packet = decode_packet(parse_rangepi_line(ack_line))
        ack = parse_ack_payload(ack_packet.payload)
        assert ack_packet.packet_type == HOPE_PACKET_TYPE_ACK
        assert ack.command_id == 40
        assert ack_status_name(ack.status) == "ok"
        assert "ping" in ack.message
        assert engine.handle_raw_packet(parse_rangepi_line(ack_line))["accepted"]

        telemetry_packet = decode_packet(parse_rangepi_line(telemetry_line))
        telemetry = parse_telemetry_payload(telemetry_packet.payload)
        assert telemetry_packet.packet_type == HOPE_PACKET_TYPE_TELEMETRY
        assert telemetry.satellites == 9

        _write_command(link, 41, COMMAND_OPCODE_SELF_TEST)
        selftest_ack = link.read_line()
        diagnostic_line = link.read_line()
        assert selftest_ack is not None
        assert diagnostic_line is not None
        diagnostic_packet = decode_packet(parse_rangepi_line(diagnostic_line))
        diagnostic = parse_diagnostic_payload(diagnostic_packet.payload)
        assert diagnostic_packet.packet_type == HOPE_PACKET_TYPE_DIAGNOSTIC
        assert diagnostic.i2c_devices_seen == 2

        _write_command(link, 42, COMMAND_OPCODE_ROTATE_SESSION)
        rotate_ack = link.read_line()
        assert rotate_ack is not None
        rotate_ack_packet = decode_packet(parse_rangepi_line(rotate_ack))
        assert rotate_ack_packet.packet_type == HOPE_PACKET_TYPE_ACK

        seen_messages: set[int] = set()
        for _ in range(64):
            line = link.read_line()
            if line is None:
                break
            packet = decode_packet(parse_rangepi_line(line))
            assert packet.packet_type == HOPE_PACKET_TYPE_HANDSHAKE
            fragment = parse_fragment_payload(packet.payload)
            seen_messages.add(fragment.message_type)

        assert LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY in seen_messages
        assert LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY in seen_messages
        assert LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE in seen_messages
    finally:
        link.close()

    print("PASS: RangePi simulator")


if __name__ == "__main__":
    main()
