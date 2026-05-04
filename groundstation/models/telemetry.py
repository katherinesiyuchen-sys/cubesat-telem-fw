from dataclasses import dataclass
import struct


TELEMETRY_PAYLOAD_FORMAT = ">iihBB"
TELEMETRY_PAYLOAD_LEN = struct.calcsize(TELEMETRY_PAYLOAD_FORMAT)


@dataclass
class Telemetry:
    latitude: float
    longitude: float
    temperature_c: float
    fix_type: int
    satellites: int


def build_telemetry_payload(
    latitude: float,
    longitude: float,
    temperature_c: float,
    fix_type: int,
    satellites: int,
) -> bytes:
    return struct.pack(
        TELEMETRY_PAYLOAD_FORMAT,
        int(round(latitude * 10_000_000)),
        int(round(longitude * 10_000_000)),
        int(round(temperature_c * 10)),
        fix_type,
        satellites,
    )


def parse_telemetry_payload(payload: bytes) -> Telemetry:
    if len(payload) != TELEMETRY_PAYLOAD_LEN:
        raise ValueError(
            f"telemetry payload must be {TELEMETRY_PAYLOAD_LEN} bytes, got {len(payload)}"
        )

    latitude_e7, longitude_e7, temperature_c_x10, fix_type, satellites = struct.unpack(
        TELEMETRY_PAYLOAD_FORMAT,
        payload,
    )

    return Telemetry(
        latitude=latitude_e7 / 10_000_000,
        longitude=longitude_e7 / 10_000_000,
        temperature_c=temperature_c_x10 / 10,
        fix_type=fix_type,
        satellites=satellites,
    )
