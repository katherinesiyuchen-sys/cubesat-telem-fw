from groundstation.backend.packet_parser import decode_packet, encode_fake_packet, encode_telemetry_packet
from groundstation.models.telemetry import parse_telemetry_payload


def assert_default_fake_packet() -> None:
    raw = encode_fake_packet(counter=7)
    pkt = decode_packet(raw)

    print("Decoded packet:")
    print(f"  version:    {pkt.version}")
    print(f"  type:       {pkt.packet_type}")
    print(f"  src:        {pkt.src_id}")
    print(f"  dst:        {pkt.dst_id}")
    print(f"  session:    0x{pkt.session_id:08X}")
    print(f"  counter:    {pkt.counter}")
    print(f"  timestamp:  {pkt.timestamp}")
    telemetry = parse_telemetry_payload(pkt.payload)
    print(f"  latitude:   {telemetry.latitude:.7f}")
    print(f"  longitude:  {telemetry.longitude:.7f}")
    print(f"  temp:       {telemetry.temperature_c:.1f}C")
    print(f"  fix/sats:   {telemetry.fix_type}/{telemetry.satellites}")

    assert pkt.version == 1
    assert pkt.packet_type == 1
    assert pkt.src_id == 1
    assert pkt.dst_id == 2
    assert pkt.session_id == 0x12345678
    assert pkt.counter == 7
    assert telemetry.latitude == 37.8715
    assert telemetry.longitude == -122.273
    assert telemetry.temperature_c == 24.5
    assert telemetry.fix_type == 3
    assert telemetry.satellites == 8


def assert_custom_demo_packet() -> None:
    raw = encode_telemetry_packet(
        counter=12,
        latitude=38.1234567,
        longitude=-121.7654321,
        temperature_c=26.2,
        fix_type=3,
        satellites=10,
        timestamp=24,
    )
    pkt = decode_packet(raw)
    telemetry = parse_telemetry_payload(pkt.payload)

    assert pkt.counter == 12
    assert pkt.timestamp == 24
    assert telemetry.latitude == 38.1234567
    assert telemetry.longitude == -121.7654321
    assert telemetry.temperature_c == 26.2
    assert telemetry.satellites == 10


def main():
    assert_default_fake_packet()
    assert_custom_demo_packet()

    print("PASS: packet parser")

if __name__ == "__main__":
    main()
