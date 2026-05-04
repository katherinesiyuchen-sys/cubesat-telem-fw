from dataclasses import dataclass
import struct


DIAGNOSTIC_PAYLOAD_FORMAT = ">BBHHHIIhhhhhB12B"
DIAGNOSTIC_PAYLOAD_LEN = struct.calcsize(DIAGNOSTIC_PAYLOAD_FORMAT)

DIAGNOSTIC_STATUS_PASS = 0
DIAGNOSTIC_STATUS_WARN = 1
DIAGNOSTIC_STATUS_FAIL = 2

DIAGNOSTIC_CHECKS = {
    1 << 0: "pin-map",
    1 << 1: "i2c-bus",
    1 << 2: "gnss-uart",
    1 << 3: "lora-radio",
    1 << 4: "secure-rng",
    1 << 5: "nvs-config",
}

DIAGNOSTIC_PIN_NAMES = (
    "status_led",
    "lora_mosi",
    "lora_miso",
    "lora_sclk",
    "lora_cs",
    "lora_reset",
    "lora_busy",
    "lora_dio1",
    "gnss_tx",
    "gnss_rx",
    "i2c_sda",
    "i2c_scl",
)


@dataclass
class DiagnosticReport:
    version: int
    overall_status: int
    passed_mask: int
    warning_mask: int
    failed_mask: int
    uptime_s: int
    boot_count: int
    lora_status: int
    gnss_status: int
    i2c_status: int
    rng_status: int
    nvs_status: int
    i2c_devices_seen: int
    pins: dict[str, int]


def diagnostic_status_name(status: int) -> str:
    if status == DIAGNOSTIC_STATUS_PASS:
        return "PASS"
    if status == DIAGNOSTIC_STATUS_WARN:
        return "WARN"
    if status == DIAGNOSTIC_STATUS_FAIL:
        return "FAIL"
    return "UNKNOWN"


def diagnostic_mask_names(mask: int) -> list[str]:
    return [name for bit, name in DIAGNOSTIC_CHECKS.items() if mask & bit]


def build_diagnostic_payload(
    *,
    version: int = 1,
    overall_status: int = DIAGNOSTIC_STATUS_WARN,
    passed_mask: int = 0x0039,
    warning_mask: int = 0x0006,
    failed_mask: int = 0x0000,
    uptime_s: int = 3,
    boot_count: int = 1,
    lora_status: int = 0,
    gnss_status: int = 263,
    i2c_status: int = 261,
    rng_status: int = 0,
    nvs_status: int = 0,
    i2c_devices_seen: int = 0,
    pins: tuple[int, ...] = (2, 23, 19, 18, 5, 14, 27, 26, 17, 16, 21, 22),
) -> bytes:
    if len(pins) != len(DIAGNOSTIC_PIN_NAMES):
        raise ValueError(f"diagnostic pins must contain {len(DIAGNOSTIC_PIN_NAMES)} values")

    return struct.pack(
        DIAGNOSTIC_PAYLOAD_FORMAT,
        version,
        overall_status,
        passed_mask,
        warning_mask,
        failed_mask,
        uptime_s,
        boot_count,
        lora_status,
        gnss_status,
        i2c_status,
        rng_status,
        nvs_status,
        i2c_devices_seen,
        *pins,
    )


def parse_diagnostic_payload(payload: bytes) -> DiagnosticReport:
    if len(payload) != DIAGNOSTIC_PAYLOAD_LEN:
        raise ValueError(
            f"diagnostic payload must be {DIAGNOSTIC_PAYLOAD_LEN} bytes, got {len(payload)}"
        )

    fields = struct.unpack(DIAGNOSTIC_PAYLOAD_FORMAT, payload)
    pins = dict(zip(DIAGNOSTIC_PIN_NAMES, fields[13:], strict=True))

    return DiagnosticReport(
        version=fields[0],
        overall_status=fields[1],
        passed_mask=fields[2],
        warning_mask=fields[3],
        failed_mask=fields[4],
        uptime_s=fields[5],
        boot_count=fields[6],
        lora_status=fields[7],
        gnss_status=fields[8],
        i2c_status=fields[9],
        rng_status=fields[10],
        nvs_status=fields[11],
        i2c_devices_seen=fields[12],
        pins=pins,
    )
