import binascii
import re

from groundstation.backend.packet_parser import HOPE_HEADER_LEN, HOPE_PACKET_TYPE_ACK, HOPE_PACKET_TYPE_COMMAND, HOPE_PACKET_TYPE_DIAGNOSTIC, HOPE_PACKET_TYPE_TELEMETRY
from groundstation.models.command import ack_status_name, command_name_from_opcode, parse_ack_payload, parse_command_payload
from groundstation.models.diagnostic import diagnostic_mask_names, diagnostic_status_name, parse_diagnostic_payload
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
    status = "ACCEPTED" if result["accepted"] else "REJECTED"

    if packet.packet_type == HOPE_PACKET_TYPE_TELEMETRY:
        telemetry = parse_telemetry_payload(packet.payload)
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

    if packet.packet_type == HOPE_PACKET_TYPE_DIAGNOSTIC:
        diagnostic = parse_diagnostic_payload(packet.payload)
        warn = ",".join(diagnostic_mask_names(diagnostic.warning_mask)) or "none"
        fail = ",".join(diagnostic_mask_names(diagnostic.failed_mask)) or "none"
        return (
            f"{status} "
            f"src={packet.src_id} "
            f"dst={packet.dst_id} "
            f"session=0x{packet.session_id:08X} "
            f"counter={packet.counter} "
            f"diag={diagnostic_status_name(diagnostic.overall_status)} "
            f"boot={diagnostic.boot_count} "
            f"i2c_seen={diagnostic.i2c_devices_seen} "
            f"warn={warn} "
            f"fail={fail}"
        )

    if packet.packet_type == HOPE_PACKET_TYPE_COMMAND:
        command = parse_command_payload(packet.payload)
        return (
            f"{status} "
            f"src={packet.src_id} "
            f"dst={packet.dst_id} "
            f"session=0x{packet.session_id:08X} "
            f"counter={packet.counter} "
            f"cmd_id={command.command_id} "
            f"opcode={command_name_from_opcode(command.opcode)}"
        )

    if packet.packet_type == HOPE_PACKET_TYPE_ACK:
        ack = parse_ack_payload(packet.payload)
        return (
            f"{status} "
            f"src={packet.src_id} "
            f"dst={packet.dst_id} "
            f"session=0x{packet.session_id:08X} "
            f"counter={packet.counter} "
            f"ack_cmd={ack.command_id} "
            f"ack_status={ack_status_name(ack.status)} "
            f"detail={ack.detail_code} "
            f"msg={ack.message}"
        )

    return (
        f"{status} "
        f"src={packet.src_id} "
        f"dst={packet.dst_id} "
        f"session=0x{packet.session_id:08X} "
        f"counter={packet.counter} "
        f"type={packet.packet_type} "
        f"payload_len={len(packet.payload)}"
    )
