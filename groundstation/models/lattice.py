import struct
from dataclasses import dataclass

LATTICE_PAYLOAD_VERSION = 1
LATTICE_FRAGMENT_HEADER_LEN = 12
HOPE_MAX_PAYLOAD_LEN = 128
LATTICE_FRAGMENT_DATA_MAX = HOPE_MAX_PAYLOAD_LEN - LATTICE_FRAGMENT_HEADER_LEN
LATTICE_MAX_OBJECT_LEN = 2420

LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY = 1
LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY = 2
LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT = 3
LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY = 4
LATTICE_MSG_SESSION_CONFIRM = 5
LATTICE_MSG_STATUS = 6
LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE = 7
LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE = 8

MLKEM512_PUBLIC_KEY_LEN = 800
MLKEM512_SECRET_KEY_LEN = 1632
MLKEM512_CIPHERTEXT_LEN = 768
MLKEM_SHARED_SECRET_LEN = 32
MLDSA44_PUBLIC_KEY_LEN = 1312
MLDSA44_SECRET_KEY_LEN = 2560
MLDSA44_SIGNATURE_LEN = 2420

_FRAGMENT_HEADER = ">BBHHHHH"


@dataclass
class LatticeFragment:
    message_type: int
    transfer_id: int
    fragment_index: int
    fragment_count: int
    total_len: int
    fragment: bytes
    version: int = LATTICE_PAYLOAD_VERSION


MESSAGE_NAMES = {
    LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY: "node-mlkem-public-key",
    LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY: "node-mldsa-public-key",
    LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT: "ground-mlkem-ciphertext",
    LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY: "ground-mldsa-public-key",
    LATTICE_MSG_SESSION_CONFIRM: "session-confirm",
    LATTICE_MSG_STATUS: "status",
    LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE: "node-handshake-signature",
    LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE: "ground-handshake-signature",
}


def message_name(message_type: int) -> str:
    return MESSAGE_NAMES.get(message_type, f"unknown-{message_type}")


def fragment_count(total_len: int) -> int:
    if total_len <= 0 or total_len > LATTICE_MAX_OBJECT_LEN:
        return 0
    return (total_len + LATTICE_FRAGMENT_DATA_MAX - 1) // LATTICE_FRAGMENT_DATA_MAX


def _validate_message_type(message_type: int) -> None:
    if message_type not in MESSAGE_NAMES:
        raise ValueError(f"unsupported lattice message type: {message_type}")


def build_fragment_payload(message_type: int, transfer_id: int, fragment_index: int, obj: bytes) -> bytes:
    _validate_message_type(message_type)
    if not obj or len(obj) > LATTICE_MAX_OBJECT_LEN:
        raise ValueError("invalid lattice object length")

    count = fragment_count(len(obj))
    if fragment_index < 0 or fragment_index >= count:
        raise ValueError("fragment index out of range")

    offset = fragment_index * LATTICE_FRAGMENT_DATA_MAX
    fragment = obj[offset:offset + LATTICE_FRAGMENT_DATA_MAX]
    return struct.pack(
        _FRAGMENT_HEADER,
        LATTICE_PAYLOAD_VERSION,
        message_type & 0xFF,
        transfer_id & 0xFFFF,
        fragment_index & 0xFFFF,
        count & 0xFFFF,
        len(obj) & 0xFFFF,
        len(fragment) & 0xFFFF,
    ) + fragment


def parse_fragment_payload(payload: bytes) -> LatticeFragment:
    header_len = struct.calcsize(_FRAGMENT_HEADER)
    if len(payload) < header_len:
        raise ValueError("lattice fragment too short")

    version, message_type, transfer_id, index, count, total_len, fragment_len = struct.unpack(
        _FRAGMENT_HEADER,
        payload[:header_len],
    )
    if version != LATTICE_PAYLOAD_VERSION:
        raise ValueError("unsupported lattice payload version")
    _validate_message_type(message_type)
    if total_len <= 0 or total_len > LATTICE_MAX_OBJECT_LEN:
        raise ValueError("invalid lattice total length")
    if count != fragment_count(total_len):
        raise ValueError("invalid lattice fragment count")
    if index >= count or fragment_len <= 0 or fragment_len > LATTICE_FRAGMENT_DATA_MAX:
        raise ValueError("invalid lattice fragment index/length")
    if len(payload) != header_len + fragment_len:
        raise ValueError("lattice fragment length mismatch")

    offset = index * LATTICE_FRAGMENT_DATA_MAX
    expected_len = min(LATTICE_FRAGMENT_DATA_MAX, total_len - offset)
    if fragment_len != expected_len:
        raise ValueError("unexpected lattice fragment length")

    return LatticeFragment(
        version=version,
        message_type=message_type,
        transfer_id=transfer_id,
        fragment_index=index,
        fragment_count=count,
        total_len=total_len,
        fragment=payload[header_len:],
    )


class LatticeReassembler:
    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self.message_type: int | None = None
        self.transfer_id: int | None = None
        self.total_len = 0
        self.fragment_count = 0
        self._received: dict[int, bytes] = {}

    def add(self, fragment: LatticeFragment) -> bytes | None:
        same_transfer = (
            self.message_type == fragment.message_type
            and self.transfer_id == fragment.transfer_id
            and self.total_len == fragment.total_len
            and self.fragment_count == fragment.fragment_count
        )
        if not same_transfer:
            self.message_type = fragment.message_type
            self.transfer_id = fragment.transfer_id
            self.total_len = fragment.total_len
            self.fragment_count = fragment.fragment_count
            self._received = {}

        self._received.setdefault(fragment.fragment_index, fragment.fragment)
        if len(self._received) != self.fragment_count:
            return None

        data = b"".join(self._received[index] for index in range(self.fragment_count))
        if len(data) != self.total_len:
            raise ValueError("reassembled lattice object length mismatch")
        return data
