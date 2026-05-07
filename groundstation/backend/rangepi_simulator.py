import math
import queue
import time
from dataclasses import dataclass

from groundstation.backend.packet_parser import (
    HOPE_PACKET_TYPE_COMMAND,
    HOPE_PACKET_TYPE_HANDSHAKE,
    decode_packet,
    encode_ack_packet,
    encode_diagnostic_packet,
    encode_handshake_packet,
    encode_telemetry_packet,
)
from groundstation.models.command import (
    COMMAND_ACK_STATUS_OK,
    COMMAND_OPCODE_CONNECT,
    COMMAND_OPCODE_OPEN_DOWNLINK,
    COMMAND_OPCODE_PING,
    COMMAND_OPCODE_RESUME_TELEMETRY,
    COMMAND_OPCODE_ROTATE_SESSION,
    COMMAND_OPCODE_SELF_TEST,
    COMMAND_OPCODE_TELEMETRY_NOW,
    command_name_from_opcode,
    parse_command_payload,
)
from groundstation.models.lattice import (
    LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE,
    LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE,
    LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY,
    LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY,
    MLDSA44_PUBLIC_KEY_LEN,
    MLDSA44_SIGNATURE_LEN,
    MLKEM512_PUBLIC_KEY_LEN,
    fragment_count,
    parse_fragment_payload,
)


@dataclass
class _VirtualNode:
    src_id: int
    dst_id: int
    session_id: int
    latitude: float
    longitude: float
    temperature_c: float
    satellites: int
    phase: float


class RangePiSimulator:
    """
    In-memory RangePi bridge for dashboard testing.

    It accepts the same serial command the real bridge should accept:
    ``TX <packet-hex>``. Command packets produce ACK packets and, for useful
    commands, follow-on telemetry/diagnostic packets.
    """

    def __init__(self, timeout: float = 1.0) -> None:
        self.timeout = timeout
        self._lines: queue.Queue[bytes] = queue.Queue()
        self._node_counter = 9000
        self._last_rx_counter_by_session: dict[int, int] = {}
        self._handled_command_ids: set[int] = set()
        self._paused = False
        self._live_interval_s = 0.75
        self._next_live_emit_at = time.monotonic() + self._live_interval_s
        self._live_index = 0
        self._nodes = [
            _VirtualNode(1, 2, 0xA13C91E0, 37.7749, -122.4194, 23.2, 9, 0.0),
            _VirtualNode(3, 2, 0x42D9BB10, 37.8715, -122.2730, 22.6, 8, 1.4),
            _VirtualNode(4, 2, 0x93AF6721, 37.8044, -122.2712, 24.1, 7, 2.8),
            _VirtualNode(5, 2, 0x0FD21A90, 37.3382, -121.8863, 25.0, 10, 4.2),
            _VirtualNode(6, 2, 0x7E219A04, 37.9735, -122.5311, 23.5, 10, 5.6),
        ]

    def close(self) -> None:
        while not self._lines.empty():
            try:
                self._lines.get_nowait()
            except queue.Empty:
                break

    def read_line(self) -> bytes | None:
        try:
            return self._lines.get(timeout=self.timeout)
        except queue.Empty:
            return self._build_live_telemetry_line()

    def write_line(self, line: bytes | str) -> None:
        payload = line.encode("utf-8") if isinstance(line, str) else line
        stripped = payload.strip()
        if not stripped:
            return

        if not stripped.upper().startswith(b"TX "):
            self._lines.put(b"SIM: expected TX <packet-hex>")
            return

        try:
            raw_packet = bytes.fromhex(stripped[3:].decode("ascii"))
        except ValueError:
            self._lines.put(b"SIM: invalid TX hex")
            return

        for response in self._handle_tx_packet(raw_packet):
            self._lines.put(response)

    def _next_counter(self) -> int:
        self._node_counter += 1
        return self._node_counter

    def _radio_line(self, raw_packet: bytes) -> bytes:
        return b"RX: " + raw_packet.hex().upper().encode("ascii") + b" RSSI=-62 SNR=9.4"

    def _handle_tx_packet(self, raw_packet: bytes) -> list[bytes]:
        try:
            packet = decode_packet(raw_packet)
        except Exception as exc:
            return [f"SIM: decode failed {exc}".encode("utf-8")]

        if packet.packet_type != HOPE_PACKET_TYPE_COMMAND:
            if packet.packet_type == HOPE_PACKET_TYPE_HANDSHAKE:
                self._track_ground_handshake(packet)
                return []
            return [b"SIM: non-command TX ignored"]

        try:
            command = parse_command_payload(packet.payload)
        except Exception as exc:
            return [f"SIM: command decode failed {exc}".encode("utf-8")]

        last_rx = self._last_rx_counter_by_session.get(packet.session_id, 0)
        if packet.counter <= last_rx:
            return [f"SIM: replay rejected counter={packet.counter}".encode("utf-8")]
        self._last_rx_counter_by_session[packet.session_id] = packet.counter
        self._track_command_session(packet.dst_id, packet.session_id)

        if command.command_id in self._handled_command_ids:
            message = "duplicate command ack"
        else:
            self._handled_command_ids.add(command.command_id)
            message = self._apply_command(command.opcode, command.arg)

        responses = [
            self._radio_line(
                encode_ack_packet(
                    command_id=command.command_id,
                    status=COMMAND_ACK_STATUS_OK,
                    counter=self._next_counter(),
                    session_id=packet.session_id,
                    timestamp=int(time.time()),
                    src_id=packet.dst_id,
                    dst_id=packet.src_id,
                    message=message,
                )
            )
        ]

        if command.opcode == COMMAND_OPCODE_SELF_TEST:
            responses.append(self._radio_line(self._build_diagnostic(packet.session_id, packet.dst_id, packet.src_id)))
        elif command.opcode == COMMAND_OPCODE_ROTATE_SESSION:
            responses.extend(self._build_lattice_identity(packet.session_id, packet.dst_id, packet.src_id, command.command_id))
        elif command.opcode in {
            COMMAND_OPCODE_PING,
            COMMAND_OPCODE_TELEMETRY_NOW,
            COMMAND_OPCODE_OPEN_DOWNLINK,
            COMMAND_OPCODE_CONNECT,
            COMMAND_OPCODE_RESUME_TELEMETRY,
        } and not self._paused:
            node = self._node_for_src(packet.dst_id) or self._node_for_session(packet.session_id)
            responses.append(self._radio_line(self._build_telemetry(packet.session_id, packet.dst_id, packet.src_id, node)))

        return responses

    def _node_for_src(self, src_id: int) -> _VirtualNode | None:
        for node in self._nodes:
            if node.src_id == src_id:
                return node
        return None

    def _node_for_session(self, session_id: int) -> _VirtualNode | None:
        for node in self._nodes:
            if node.session_id == session_id:
                return node
        return None

    def _track_command_session(self, node_src_id: int, session_id: int) -> None:
        node = self._node_for_src(node_src_id)
        if node is not None:
            node.session_id = session_id

    def _track_ground_handshake(self, packet) -> None:
        try:
            fragment = parse_fragment_payload(packet.payload)
        except Exception:
            return
        if fragment.message_type != LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE:
            return
        self._track_command_session(packet.dst_id, packet.session_id)

    def _build_live_telemetry_line(self) -> bytes | None:
        now = time.monotonic()
        if self._paused or now < self._next_live_emit_at:
            return None

        self._next_live_emit_at = now + self._live_interval_s
        node = self._nodes[self._live_index % len(self._nodes)]
        self._live_index += 1
        return self._radio_line(self._build_telemetry(node.session_id, node.src_id, node.dst_id, node))

    def _apply_command(self, opcode: int, arg: bytes = b"") -> str:
        name = command_name_from_opcode(opcode)
        if name in {"pause", "isolate"}:
            self._paused = True
        elif name in {"resume", "connect", "downlink", "ping", "telemetry-now"}:
            self._paused = False
        elif name == "cadence":
            value = arg.decode("utf-8", errors="ignore").lower()
            if value == "fast":
                self._live_interval_s = 0.35
            elif value == "slow":
                self._live_interval_s = 1.8
            elif value in {"normal", "auto"}:
                self._live_interval_s = 0.75
        return f"sim {name} accepted"

    def _build_telemetry(self, session_id: int, src_id: int, dst_id: int, node: _VirtualNode | None = None) -> bytes:
        node = node or self._node_for_session(session_id)
        phase = (self._node_counter * 0.05) + (node.phase if node is not None else 0.0)
        latitude = (node.latitude if node is not None else 37.7749) + math.sin(phase) * 0.00045
        longitude = (node.longitude if node is not None else -122.4194) + math.cos(phase * 0.8) * 0.00045
        temperature_c = (node.temperature_c if node is not None else 23.2) + math.sin(phase * 0.7) * 0.8
        satellites = node.satellites if node is not None else 9
        speed_mps = abs(math.sin(phase * 0.4)) * 0.7
        course_deg = (phase * 17.0) % 360.0
        return encode_telemetry_packet(
            counter=self._next_counter(),
            latitude=latitude,
            longitude=longitude,
            temperature_c=temperature_c,
            fix_type=3,
            satellites=satellites,
            altitude_m=11.0 + math.sin(phase * 0.5) * 2.0,
            hdop=0.75 + abs(math.cos(phase)) * 0.35,
            speed_mps=speed_mps,
            course_deg=course_deg,
            fix_age_ms=120 + int(abs(math.sin(phase)) * 260),
            utc_time_ms=int((time.time() % 86400) * 1000),
            utc_date_ddmmyy=60526,
            session_id=session_id,
            timestamp=int(time.time()),
            src_id=src_id,
            dst_id=dst_id,
        )

    def _build_diagnostic(self, session_id: int, src_id: int, dst_id: int) -> bytes:
        return encode_diagnostic_packet(
            counter=self._next_counter(),
            session_id=session_id,
            timestamp=int(time.time()),
            src_id=src_id,
            dst_id=dst_id,
            boot_count=3,
            warning_mask=0x0000,
            i2c_devices_seen=2,
        )

    def _demo_object(self, length: int, seed: int) -> bytes:
        return bytes(((seed + index * 17) & 0xFF) for index in range(length))

    def _lattice_identity_objects(self, session_id: int, transfer_id: int) -> list[tuple[int, bytes, int]]:
        try:
            from groundstation.backend.pq_lattice import (
                KEM_ALG,
                SIG_ALG,
                TRANSCRIPT_ROLE_NODE_IDENTITY,
                build_transcript_digest,
                load_oqs,
            )

            oqs = load_oqs()
            with oqs.KeyEncapsulation(KEM_ALG) as kem:
                mlkem_public_key = kem.generate_keypair()

            signature = oqs.Signature(SIG_ALG)
            try:
                mldsa_public_key = signature.generate_keypair()
                digest = build_transcript_digest(
                    TRANSCRIPT_ROLE_NODE_IDENTITY,
                    transfer_id & 0xFFFF,
                    session_id,
                    mlkem_public_key,
                    mldsa_public_key,
                )
                node_signature = signature.sign(digest)
            finally:
                free = getattr(signature, "free", None)
                if callable(free):
                    free()

            return [
                (LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY, mlkem_public_key, transfer_id & 0xFFFF),
                (LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY, mldsa_public_key, transfer_id & 0xFFFF),
                (LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE, node_signature, transfer_id & 0xFFFF),
            ]
        except Exception:
            return [
                (LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY, self._demo_object(MLKEM512_PUBLIC_KEY_LEN, 0x42), transfer_id & 0xFFFF),
                (LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY, self._demo_object(MLDSA44_PUBLIC_KEY_LEN, 0xA7), transfer_id & 0xFFFF),
                (LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE, self._demo_object(MLDSA44_SIGNATURE_LEN, 0x5C), transfer_id & 0xFFFF),
            ]

    def _build_lattice_identity(self, session_id: int, src_id: int, dst_id: int, transfer_id: int) -> list[bytes]:
        packets: list[bytes] = []
        objects = self._lattice_identity_objects(session_id, transfer_id)
        for message_type, obj, tx_id in objects:
            for index in range(fragment_count(len(obj))):
                packets.append(
                    self._radio_line(
                        encode_handshake_packet(
                            message_type=message_type,
                            transfer_id=tx_id,
                            fragment_index=index,
                            obj=obj,
                            counter=self._next_counter(),
                            session_id=session_id,
                            timestamp=int(time.time()),
                            src_id=src_id,
                            dst_id=dst_id,
                        )
                    )
                )
        return packets
