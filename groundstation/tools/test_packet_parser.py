from groundstation.backend.packet_parser import decode_packet, encode_ack_packet, encode_command_packet, encode_diagnostic_packet, encode_fake_packet, encode_telemetry_packet
from groundstation.models.command import COMMAND_ACK_STATUS_OK, COMMAND_AUTH_TAG_LEN, COMMAND_FLAG_AUTH_PRESENT, COMMAND_OPCODE_PING, ack_status_name, parse_ack_payload, parse_command_payload
from groundstation.models.diagnostic import diagnostic_mask_names, diagnostic_status_name, parse_diagnostic_payload
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
    print(f"  alt/hdop:   {telemetry.altitude_m:.1f}m/{telemetry.hdop:.2f}")
    print(f"  age:        {telemetry.fix_age_ms}ms")

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
    assert telemetry.payload_version == 2
    assert telemetry.altitude_m == 11.0
    assert telemetry.hdop == 0.9


def assert_custom_demo_packet() -> None:
    raw = encode_telemetry_packet(
        counter=12,
        latitude=38.1234567,
        longitude=-121.7654321,
        temperature_c=26.2,
        fix_type=3,
        satellites=10,
        altitude_m=31.5,
        hdop=1.25,
        speed_mps=2.4,
        course_deg=270.1,
        fix_age_ms=330,
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
    assert telemetry.altitude_m == 31.5
    assert telemetry.hdop == 1.25
    assert telemetry.speed_mps == 2.4
    assert telemetry.course_deg == 270.1
    assert telemetry.fix_age_ms == 330


def assert_legacy_telemetry_payload() -> None:
    import struct

    payload = struct.pack(">iihBB", 378715000, -1222730000, 245, 3, 8)
    telemetry = parse_telemetry_payload(payload)

    assert telemetry.payload_version == 1
    assert telemetry.latitude == 37.8715
    assert telemetry.longitude == -122.273
    assert telemetry.altitude_m == 0.0
    assert telemetry.hdop == 0.0


def assert_diagnostic_packet() -> None:
    raw = encode_diagnostic_packet(counter=22, boot_count=4, warning_mask=0x0006)
    pkt = decode_packet(raw)
    diagnostic = parse_diagnostic_payload(pkt.payload)

    assert pkt.packet_type == 5
    assert pkt.counter == 22
    assert diagnostic.boot_count == 4
    assert diagnostic_status_name(diagnostic.overall_status) == "WARN"
    assert diagnostic.pins["lora_mosi"] == 23
    assert "i2c-bus" in diagnostic_mask_names(diagnostic.warning_mask)
    assert "gnss-uart" in diagnostic_mask_names(diagnostic.warning_mask)


def assert_command_and_ack_packets() -> None:
    raw_command = encode_command_packet(command_id=91, opcode=COMMAND_OPCODE_PING, session_id=0xA1B2C3D4)
    command_pkt = decode_packet(raw_command)
    command = parse_command_payload(command_pkt.payload)

    assert command_pkt.packet_type == 6
    assert command_pkt.src_id == 2
    assert command_pkt.dst_id == 1
    assert command_pkt.session_id == 0xA1B2C3D4
    assert command_pkt.counter == 91
    assert command.command_id == 91
    assert command.opcode == COMMAND_OPCODE_PING
    assert command.auth_key_id == 0
    assert command.auth_tag == bytes(COMMAND_AUTH_TAG_LEN)

    raw_auth_command = encode_command_packet(
        command_id=92,
        opcode=COMMAND_OPCODE_PING,
        session_id=0xA1B2C3D4,
        auth_key=bytes(range(32)),
    )
    auth_command = parse_command_payload(decode_packet(raw_auth_command).payload)
    assert auth_command.flags & COMMAND_FLAG_AUTH_PRESENT
    assert auth_command.auth_key_id == 1
    assert auth_command.auth_tag != bytes(COMMAND_AUTH_TAG_LEN)

    raw_ack = encode_ack_packet(
        command_id=91,
        status=COMMAND_ACK_STATUS_OK,
        counter=42,
        session_id=0xA1B2C3D4,
        message="pong telemetry queued",
    )
    ack_pkt = decode_packet(raw_ack)
    ack = parse_ack_payload(ack_pkt.payload)

    assert ack_pkt.packet_type == 4
    assert ack_pkt.src_id == 1
    assert ack_pkt.dst_id == 2
    assert ack.command_id == 91
    assert ack_status_name(ack.status) == "ok"
    assert ack.message == "pong telemetry queued"


def main():
    assert_default_fake_packet()
    assert_custom_demo_packet()
    assert_legacy_telemetry_payload()
    assert_diagnostic_packet()
    assert_command_and_ack_packets()

    print("PASS: packet parser")

if __name__ == "__main__":
    main()
