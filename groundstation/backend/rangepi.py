import binascii
import re

from groundstation.backend.packet_parser import HOPE_HEADER_LEN
from groundstation.models.telemetry import parse_telemetry_payload


_CONTIGUOUS_HEX_RE = re.compile(rb"(?i)[0-9a-f]{" + str(HOPE_HEADER_LEN * 2).encode("ascii") + rb",}")
_SPACED_HEX_RE = re.compile(rb"(?i)(?:[0-9a-f]{2}[\s:,-]*){" + str(HOPE_HEADER_LEN).encode("ascii") + rb",}")
_WHOLE_SPACED_HEX_RE = re.compile(rb"(?i)^\s*(?:[0-9a-f]{2}[\s:,-]*){" + str(HOPE_HEADER_LEN).encode("ascii") + rb",}\s*$")


def parse_rangepi_line(line: bytes) -> bytes:
    """
    Convert one RangePi serial line into raw HOPE packet bytes.

    Supported forms:
      - 010100010002...
      - RX: 010100010002...
      - RX 01 01 00 01 00 02 ...
      - +RCV=1,32,010100010002..., -70, 9

    If no packet-like hex payload is found, return the stripped line as raw
    bytes so a true binary serial bridge can still be handled by the decoder.
    """
    stripped = line.strip()
    if not stripped:
        raise ValueError("empty RangePi line")

    for match in _CONTIGUOUS_HEX_RE.finditer(stripped):
        try:
            return binascii.unhexlify(match.group(0))
        except binascii.Error:
            continue

    if _WHOLE_SPACED_HEX_RE.fullmatch(stripped):
        compact = re.sub(rb"[^0-9A-Fa-f]", b"", stripped)
        if len(compact) >= HOPE_HEADER_LEN * 2 and len(compact) % 2 == 0:
            try:
                return binascii.unhexlify(compact)
            except binascii.Error:
                pass

    for match in _SPACED_HEX_RE.finditer(stripped):
        compact_match = re.sub(rb"[^0-9A-Fa-f]", b"", match.group(0))
        if len(compact_match) < HOPE_HEADER_LEN * 2 or len(compact_match) % 2 != 0:
            continue
        try:
            return binascii.unhexlify(compact_match)
        except binascii.Error:
            continue

    return stripped


def format_packet_result(result: dict) -> str:
    packet = result["packet"]
    telemetry = parse_telemetry_payload(packet.payload)
    status = "ACCEPTED" if result["accepted"] else "REJECTED"
    return (
        f"{status} "
        f"src={packet.src_id} "
        f"dst={packet.dst_id} "
        f"session=0x{packet.session_id:08X} "
        f"counter={packet.counter} "
        f"lat={telemetry.latitude:.5f} "
        f"lon={telemetry.longitude:.5f} "
        f"temp={telemetry.temperature_c:.1f}C "
        f"fix={telemetry.fix_type} "
        f"sats={telemetry.satellites}"
    )
