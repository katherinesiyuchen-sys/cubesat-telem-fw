import struct
from dataclasses import dataclass

COMMAND_PAYLOAD_VERSION = 1
COMMAND_MAX_ARG_LEN = 48
COMMAND_AUTH_TAG_LEN = 16
COMMAND_FLAG_AUTH_PRESENT = 0x01
COMMAND_ACK_MAX_MESSAGE_LEN = 48

COMMAND_OPCODE_SELF_TEST = 1
COMMAND_OPCODE_PING = 2
COMMAND_OPCODE_TELEMETRY_NOW = 3
COMMAND_OPCODE_PAUSE_TELEMETRY = 4
COMMAND_OPCODE_RESUME_TELEMETRY = 5
COMMAND_OPCODE_ROTATE_SESSION = 6
COMMAND_OPCODE_OPEN_DOWNLINK = 7
COMMAND_OPCODE_ISOLATE = 8
COMMAND_OPCODE_CONNECT = 9
COMMAND_OPCODE_ARM = 10

COMMAND_ACK_STATUS_OK = 0
COMMAND_ACK_STATUS_REJECTED = 1
COMMAND_ACK_STATUS_ERROR = 2

_REQUEST_HEADER = ">BIBBH16sB"
_ACK_HEADER = ">BBIBhB"


@dataclass
class CommandRequest:
    command_id: int
    opcode: int
    flags: int = 0
    auth_key_id: int = 0
    auth_tag: bytes = bytes(COMMAND_AUTH_TAG_LEN)
    arg: bytes = b""
    version: int = COMMAND_PAYLOAD_VERSION


@dataclass
class CommandAck:
    acked_type: int
    command_id: int
    status: int
    detail_code: int
    message: str
    version: int = COMMAND_PAYLOAD_VERSION


COMMAND_NAME_TO_OPCODE = {
    "selftest": COMMAND_OPCODE_SELF_TEST,
    "diag": COMMAND_OPCODE_SELF_TEST,
    "diagnostic": COMMAND_OPCODE_SELF_TEST,
    "ping": COMMAND_OPCODE_PING,
    "telemetry": COMMAND_OPCODE_TELEMETRY_NOW,
    "telemetry-now": COMMAND_OPCODE_TELEMETRY_NOW,
    "status": COMMAND_OPCODE_TELEMETRY_NOW,
    "pause": COMMAND_OPCODE_PAUSE_TELEMETRY,
    "resume": COMMAND_OPCODE_RESUME_TELEMETRY,
    "rotate": COMMAND_OPCODE_ROTATE_SESSION,
    "rotate-session": COMMAND_OPCODE_ROTATE_SESSION,
    "downlink": COMMAND_OPCODE_OPEN_DOWNLINK,
    "isolate": COMMAND_OPCODE_ISOLATE,
    "connect": COMMAND_OPCODE_CONNECT,
    "arm": COMMAND_OPCODE_ARM,
}

OPCODE_TO_COMMAND_NAME = {
    value: key
    for key, value in COMMAND_NAME_TO_OPCODE.items()
    if key not in {"diag", "diagnostic", "telemetry", "status", "rotate-session"}
}

ACK_STATUS_NAME = {
    COMMAND_ACK_STATUS_OK: "ok",
    COMMAND_ACK_STATUS_REJECTED: "rejected",
    COMMAND_ACK_STATUS_ERROR: "error",
}


def command_opcode_from_name(name: str) -> int:
    key = name.strip().lower()
    if key not in COMMAND_NAME_TO_OPCODE:
        raise ValueError(f"unsupported command: {name}")
    return COMMAND_NAME_TO_OPCODE[key]


def command_name_from_opcode(opcode: int) -> str:
    return OPCODE_TO_COMMAND_NAME.get(opcode, f"opcode-{opcode}")


def ack_status_name(status: int) -> str:
    return ACK_STATUS_NAME.get(status, f"status-{status}")


def build_command_payload(
    *,
    command_id: int,
    opcode: int,
    flags: int = 0,
    auth_key_id: int = 0,
    auth_tag: bytes | None = None,
    arg: bytes | str = b"",
) -> bytes:
    if isinstance(arg, str):
        arg = arg.encode("utf-8")
    if len(arg) > COMMAND_MAX_ARG_LEN:
        raise ValueError("command argument too long")
    if auth_tag is None:
        auth_tag = bytes(COMMAND_AUTH_TAG_LEN)
    if len(auth_tag) != COMMAND_AUTH_TAG_LEN:
        raise ValueError("command auth tag must be 16 bytes")

    return struct.pack(
        _REQUEST_HEADER,
        COMMAND_PAYLOAD_VERSION,
        command_id & 0xFFFFFFFF,
        opcode & 0xFF,
        flags & 0xFF,
        auth_key_id & 0xFFFF,
        auth_tag,
        len(arg),
    ) + arg


def parse_command_payload(payload: bytes) -> CommandRequest:
    header_len = struct.calcsize(_REQUEST_HEADER)
    if len(payload) < header_len:
        raise ValueError("command payload too short")

    version, command_id, opcode, flags, auth_key_id, auth_tag, arg_len = struct.unpack(_REQUEST_HEADER, payload[:header_len])
    if version != COMMAND_PAYLOAD_VERSION:
        raise ValueError("unsupported command payload version")
    if arg_len > COMMAND_MAX_ARG_LEN or len(payload) < header_len + arg_len:
        raise ValueError("command argument truncated")

    return CommandRequest(
        version=version,
        command_id=command_id,
        opcode=opcode,
        flags=flags,
        auth_key_id=auth_key_id,
        auth_tag=auth_tag,
        arg=payload[header_len:header_len + arg_len],
    )


def build_ack_payload(
    *,
    acked_type: int,
    command_id: int,
    status: int,
    detail_code: int = 0,
    message: bytes | str = b"",
) -> bytes:
    if isinstance(message, str):
        message = message.encode("utf-8")
    if len(message) > COMMAND_ACK_MAX_MESSAGE_LEN:
        message = message[:COMMAND_ACK_MAX_MESSAGE_LEN]

    return struct.pack(
        _ACK_HEADER,
        COMMAND_PAYLOAD_VERSION,
        acked_type & 0xFF,
        command_id & 0xFFFFFFFF,
        status & 0xFF,
        int(detail_code),
        len(message),
    ) + message


def parse_ack_payload(payload: bytes) -> CommandAck:
    header_len = struct.calcsize(_ACK_HEADER)
    if len(payload) < header_len:
        raise ValueError("ack payload too short")

    version, acked_type, command_id, status, detail_code, message_len = struct.unpack(_ACK_HEADER, payload[:header_len])
    if version != COMMAND_PAYLOAD_VERSION:
        raise ValueError("unsupported ack payload version")
    if message_len > COMMAND_ACK_MAX_MESSAGE_LEN or len(payload) < header_len + message_len:
        raise ValueError("ack message truncated")

    return CommandAck(
        version=version,
        acked_type=acked_type,
        command_id=command_id,
        status=status,
        detail_code=detail_code,
        message=payload[header_len:header_len + message_len].decode("utf-8", errors="replace"),
    )
