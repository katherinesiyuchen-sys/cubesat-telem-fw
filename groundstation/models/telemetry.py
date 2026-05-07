from dataclasses import dataclass
import struct


TELEMETRY_PAYLOAD_V1_FORMAT = ">iihBB"
TELEMETRY_PAYLOAD_V2_FORMAT = ">iihBBiHHHBBIII"
TELEMETRY_PAYLOAD_V1_LEN = struct.calcsize(TELEMETRY_PAYLOAD_V1_FORMAT)
TELEMETRY_PAYLOAD_LEN = struct.calcsize(TELEMETRY_PAYLOAD_V2_FORMAT)

GNSS_FIX_FLAG_RMC = 0x01
GNSS_FIX_FLAG_GGA = 0x02
GNSS_FIX_FLAG_CHECKSUM = 0x04
GNSS_FIX_FLAG_STALE = 0x80


@dataclass
class Telemetry:
    latitude: float
    longitude: float
    temperature_c: float
    fix_type: int
    satellites: int
    altitude_m: float = 0.0
    hdop: float = 0.0
    speed_mps: float = 0.0
    course_deg: float = 0.0
    fix_age_ms: int = 0
    utc_time_ms: int = 0
    utc_date_ddmmyy: int = 0
    gnss_flags: int = 0
    payload_version: int = 1


def build_telemetry_payload(
    latitude: float,
    longitude: float,
    temperature_c: float,
    fix_type: int,
    satellites: int,
    altitude_m: float = 0.0,
    hdop: float = 0.0,
    speed_mps: float = 0.0,
    course_deg: float = 0.0,
    fix_age_ms: int = 0,
    utc_time_ms: int = 0,
    utc_date_ddmmyy: int = 0,
    gnss_flags: int = GNSS_FIX_FLAG_RMC | GNSS_FIX_FLAG_GGA | GNSS_FIX_FLAG_CHECKSUM,
) -> bytes:
    return struct.pack(
        TELEMETRY_PAYLOAD_V2_FORMAT,
        int(round(latitude * 10_000_000)),
        int(round(longitude * 10_000_000)),
        int(round(temperature_c * 10)),
        fix_type,
        satellites,
        int(round(altitude_m * 10)),
        max(0, min(65535, int(round(hdop * 100)))),
        max(0, min(65535, int(round(speed_mps * 100)))),
        max(0, min(65535, int(round(course_deg * 100)))),
        gnss_flags & 0xFF,
        0,
        max(0, min(0xFFFFFFFF, int(fix_age_ms))),
        max(0, min(0xFFFFFFFF, int(utc_time_ms))),
        max(0, min(0xFFFFFFFF, int(utc_date_ddmmyy))),
    )


def parse_telemetry_payload(payload: bytes) -> Telemetry:
    if len(payload) == TELEMETRY_PAYLOAD_V1_LEN:
        latitude_e7, longitude_e7, temperature_c_x10, fix_type, satellites = struct.unpack(
            TELEMETRY_PAYLOAD_V1_FORMAT,
            payload,
        )
        return Telemetry(
            latitude=latitude_e7 / 10_000_000,
            longitude=longitude_e7 / 10_000_000,
            temperature_c=temperature_c_x10 / 10,
            fix_type=fix_type,
            satellites=satellites,
            payload_version=1,
        )

    if len(payload) != TELEMETRY_PAYLOAD_LEN:
        raise ValueError(
            f"telemetry payload must be {TELEMETRY_PAYLOAD_V1_LEN} or {TELEMETRY_PAYLOAD_LEN} bytes, got {len(payload)}"
        )

    (
        latitude_e7,
        longitude_e7,
        temperature_c_x10,
        fix_type,
        satellites,
        altitude_m_x10,
        hdop_x100,
        speed_mps_x100,
        course_deg_x100,
        gnss_flags,
        _reserved,
        fix_age_ms,
        utc_time_ms,
        utc_date_ddmmyy,
    ) = struct.unpack(
        TELEMETRY_PAYLOAD_V2_FORMAT,
        payload,
    )

    return Telemetry(
        latitude=latitude_e7 / 10_000_000,
        longitude=longitude_e7 / 10_000_000,
        temperature_c=temperature_c_x10 / 10,
        fix_type=fix_type,
        satellites=satellites,
        altitude_m=altitude_m_x10 / 10,
        hdop=hdop_x100 / 100,
        speed_mps=speed_mps_x100 / 100,
        course_deg=course_deg_x100 / 100,
        fix_age_ms=fix_age_ms,
        utc_time_ms=utc_time_ms,
        utc_date_ddmmyy=utc_date_ddmmyy,
        gnss_flags=gnss_flags,
        payload_version=2,
    )
