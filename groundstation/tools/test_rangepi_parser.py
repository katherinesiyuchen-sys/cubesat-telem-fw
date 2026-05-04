from groundstation.backend.packet_parser import encode_fake_packet
from groundstation.backend.rangepi import parse_rangepi_line


def main() -> None:
    raw = encode_fake_packet(counter=9)
    hex_packet = raw.hex().encode("ascii")

    assert parse_rangepi_line(hex_packet) == raw
    assert parse_rangepi_line(b"RX: " + hex_packet + b" RSSI=-72") == raw
    assert parse_rangepi_line(b"RX " + b" ".join(hex_packet[i:i + 2] for i in range(0, len(hex_packet), 2))) == raw
    assert parse_rangepi_line(b"+RCV=1,32," + hex_packet + b",-70,9") == raw
    assert parse_rangepi_line(raw) == raw

    print("PASS: RangePi parser")


if __name__ == "__main__":
    main()
