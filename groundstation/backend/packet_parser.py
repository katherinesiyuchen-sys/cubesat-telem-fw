import struct
from dataclasses import dataclass

HOPE_HEADER_FORMAT = ">BBHHIIIH"
HOPE_HEADER_LEN = struct.calcsize(HOPE_HEADER_FORMAT)

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

def encode_fake_packet(counter: int = 1) -> bytes:
    payload = b"lat=37.8715,lon=-122.2730,temp=24.5"

    header = struct.pack(
        HOPE_HEADER_FORMAT,
        1,              # version
        1,              # telemetry type
        1,              # src_id
        2,              # dst_id
        0x12345678,     # session_id
        counter,        # counter
        0,              # timestamp
        len(payload),
    )

    return header + payload