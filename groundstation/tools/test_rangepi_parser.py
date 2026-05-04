from groundstation.backend.packet_parser import encode_ack_packet, encode_command_packet, encode_fake_packet
from groundstation.backend.rangepi import format_packet_result, parse_rangepi_line
from groundstation.backend.event_engine import EventEngine
from groundstation.models.command import COMMAND_ACK_STATUS_OK, COMMAND_OPCODE_PING


def main() -> None:
    raw = encode_fake_packet(counter=9)
    hex_packet = raw.hex().encode("ascii")

    assert parse_rangepi_line(hex_packet) == raw
    assert parse_rangepi_line(b"RX: " + hex_packet + b" RSSI=-72") == raw
    assert parse_rangepi_line(b"RX " + b" ".join(hex_packet[i:i + 2] for i in range(0, len(hex_packet), 2))) == raw
    assert parse_rangepi_line(b"+RCV=1,32," + hex_packet + b",-70,9") == raw
    assert parse_rangepi_line(raw) == raw

    command = encode_command_packet(command_id=17, opcode=COMMAND_OPCODE_PING, counter=17)
    ack = encode_ack_packet(command_id=17, status=COMMAND_ACK_STATUS_OK, counter=18, message="pong")
    assert parse_rangepi_line(command.hex().encode("ascii")) == command
    assert "opcode=ping" in format_packet_result(EventEngine().handle_raw_packet(command))
    assert "ack_status=ok" in format_packet_result(EventEngine().handle_raw_packet(ack))

    print("PASS: RangePi parser")


if __name__ == "__main__":
    main()
