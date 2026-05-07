import hashlib
import hmac
import struct
from dataclasses import dataclass

from groundstation.models.command import COMMAND_AUTH_TAG_LEN, COMMAND_FLAG_AUTH_PRESENT, build_ack_payload, build_command_payload
from groundstation.models.diagnostic import build_diagnostic_payload
from groundstation.models.lattice import build_fragment_payload
from groundstation.models.telemetry import build_telemetry_payload

HOPE_HEADER_FORMAT = ">BBHHIIIH"
HOPE_HEADER_LEN = struct.calcsize(HOPE_HEADER_FORMAT)
HOPE_PACKET_TYPE_TELEMETRY = 1
HOPE_PACKET_TYPE_ALERT = 2
HOPE_PACKET_TYPE_HANDSHAKE = 3
HOPE_PACKET_TYPE_ACK = 4
HOPE_PACKET_TYPE_DIAGNOSTIC = 5
HOPE_PACKET_TYPE_COMMAND = 6

@dataclass
class HopePacket:
    version: int
    packet_type: int
    src_id: int
    dst_id: int
    session_id: int
    counter: int
    timestamp: int
    payload: bytes

def decode_packet(data: bytes) -> HopePacket:
    if len(data) < HOPE_HEADER_LEN:
        raise ValueError("packet too short")

    (
        version,
        packet_type,
        src_id,
        dst_id,
        session_id,
        counter,
        timestamp,
        payload_len,
    ) = struct.unpack(HOPE_HEADER_FORMAT, data[:HOPE_HEADER_LEN])

    end = HOPE_HEADER_LEN + payload_len

    if len(data) < end:
        raise ValueError("payload truncated")

    payload = data[HOPE_HEADER_LEN:end]

    return HopePacket(
        version=version,
        packet_type=packet_type,
        src_id=src_id,
        dst_id=dst_id,
        session_id=session_id,
        counter=counter,
        timestamp=timestamp,
        payload=payload,
    )

def encode_telemetry_packet(
    *,
    counter: int,
    latitude: float,
    longitude: float,
    temperature_c: float,
    fix_type: int = 3,
    satellites: int = 8,
    altitude_m: float = 0.0,
    hdop: float = 0.0,
    speed_mps: float = 0.0,
    course_deg: float = 0.0,
    fix_age_ms: int = 0,
    utc_time_ms: int = 0,
    utc_date_ddmmyy: int = 0,
    gnss_flags: int = 0x07,
    session_id: int = 0x12345678,
    timestamp: int = 0,
    src_id: int = 1,
    dst_id: int = 2,
) -> bytes:
    payload = build_telemetry_payload(
        latitude=latitude,
        longitude=longitude,
        temperature_c=temperature_c,
        fix_type=fix_type,
        satellites=satellites,
        altitude_m=altitude_m,
        hdop=hdop,
        speed_mps=speed_mps,
        course_deg=course_deg,
        fix_age_ms=fix_age_ms,
        utc_time_ms=utc_time_ms,
        utc_date_ddmmyy=utc_date_ddmmyy,
        gnss_flags=gnss_flags,
    )

    header = struct.pack(
        HOPE_HEADER_FORMAT,
        1,              # version
        HOPE_PACKET_TYPE_TELEMETRY,
        src_id,
        dst_id,
        session_id,
        counter,        # counter
        timestamp,
        len(payload),
    )

    return header + payload


def encode_fake_packet(counter: int = 1) -> bytes:
    return encode_telemetry_packet(
        counter=counter,
        latitude=37.8715,
        longitude=-122.273,
        temperature_c=24.5,
        fix_type=3,
        satellites=8,
        altitude_m=11.0,
        hdop=0.9,
        utc_time_ms=45319000,
        utc_date_ddmmyy=10626,
    )


def encode_diagnostic_packet(
    *,
    counter: int,
    session_id: int = 0x12345678,
    timestamp: int = 0,
    src_id: int = 1,
    dst_id: int = 2,
    **payload_fields,
) -> bytes:
    payload = build_diagnostic_payload(**payload_fields)
    header = struct.pack(
        HOPE_HEADER_FORMAT,
        1,
        HOPE_PACKET_TYPE_DIAGNOSTIC,
        src_id,
        dst_id,
        session_id,
        counter,
        timestamp,
        len(payload),
    )
    return header + payload


def encode_command_packet(
    *,
    command_id: int,
    opcode: int,
    counter: int | None = None,
    flags: int = 0,
    auth_key_id: int = 0,
    auth_tag: bytes | None = None,
    auth_key: bytes | None = None,
    arg: bytes | str = b"",
    session_id: int = 0x12345678,
    timestamp: int = 0,
    src_id: int = 2,
    dst_id: int = 1,
) -> bytes:
    if auth_key is not None:
        flags |= COMMAND_FLAG_AUTH_PRESENT
        if auth_key_id == 0:
            auth_key_id = 1
        auth_tag = bytes(COMMAND_AUTH_TAG_LEN)

    payload = build_command_payload(
        command_id=command_id,
        opcode=opcode,
        flags=flags,
        auth_key_id=auth_key_id,
        auth_tag=auth_tag,
        arg=arg,
    )
    header = struct.pack(
        HOPE_HEADER_FORMAT,
        1,
        HOPE_PACKET_TYPE_COMMAND,
        src_id,
        dst_id,
        session_id,
        counter if counter is not None else command_id,
        timestamp,
        len(payload),
    )

    if auth_key is None:
        return header + payload

    auth_tag = hmac.new(auth_key, header + payload, hashlib.sha256).digest()[:COMMAND_AUTH_TAG_LEN]
    payload = build_command_payload(
        command_id=command_id,
        opcode=opcode,
        flags=flags,
        auth_key_id=auth_key_id,
        auth_tag=auth_tag,
        arg=arg,
    )
    return header + payload


def encode_ack_packet(
    *,
    command_id: int,
    status: int,
    counter: int,
    acked_type: int = HOPE_PACKET_TYPE_COMMAND,
    detail_code: int = 0,
    message: bytes | str = b"",
    session_id: int = 0x12345678,
    timestamp: int = 0,
    src_id: int = 1,
    dst_id: int = 2,
) -> bytes:
    payload = build_ack_payload(
        acked_type=acked_type,
        command_id=command_id,
        status=status,
        detail_code=detail_code,
        message=message,
    )
    header = struct.pack(
        HOPE_HEADER_FORMAT,
        1,
        HOPE_PACKET_TYPE_ACK,
        src_id,
        dst_id,
        session_id,
        counter,
        timestamp,
        len(payload),
    )
    return header + payload


def encode_handshake_packet(
    *,
    message_type: int,
    transfer_id: int,
    fragment_index: int,
    obj: bytes,
    counter: int,
    session_id: int = 0x12345678,
    timestamp: int = 0,
    src_id: int = 2,
    dst_id: int = 1,
) -> bytes:
    payload = build_fragment_payload(
        message_type=message_type,
        transfer_id=transfer_id,
        fragment_index=fragment_index,
        obj=obj,
    )
    header = struct.pack(
        HOPE_HEADER_FORMAT,
        1,
        HOPE_PACKET_TYPE_HANDSHAKE,
        src_id,
        dst_id,
        session_id,
        counter,
        timestamp,
        len(payload),
    )
    return header + payload
