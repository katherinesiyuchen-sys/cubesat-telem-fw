import argparse
import csv
import hashlib
import json
import math
import os
import queue
import random
import re
import sys
import threading
import time
import webbrowser
from collections import deque
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

if __package__ in {None, ""}:
    repo_root = Path(__file__).resolve().parents[2]
    if str(repo_root) not in sys.path:
        sys.path.insert(0, str(repo_root))

from groundstation.backend.event_engine import EventEngine
from groundstation.backend.node_registry import NodeRegistry
from groundstation.backend.packet_parser import (
    HOPE_HEADER_LEN,
    HOPE_PACKET_TYPE_ACK,
    HOPE_PACKET_TYPE_COMMAND,
    HOPE_PACKET_TYPE_DIAGNOSTIC,
    HOPE_PACKET_TYPE_HANDSHAKE,
    HOPE_PACKET_TYPE_TELEMETRY,
    HopePacket,
    encode_command_packet,
)
from groundstation.backend.rangepi import format_packet_result, parse_rangepi_line
from groundstation.backend.serial_link import SerialLink
from groundstation.models.command import ack_status_name, command_opcode_from_name, parse_ack_payload
from groundstation.models.diagnostic import diagnostic_mask_names, diagnostic_status_name, parse_diagnostic_payload
from groundstation.models.lattice import message_name, parse_fragment_payload
from groundstation.models.telemetry import parse_telemetry_payload

CYAN = "#5ce7ff"
GREEN = "#7dffb0"
AMBER = "#f1d36b"
RED = "#ff4b57"
MAGENTA = "#d78cff"
BLUE = "#9bb7ff"


def _now_label() -> str:
    return time.strftime("%H:%M:%S")


@dataclass
class WebNode:
    name: str
    role: str
    session_id: int
    latitude: float
    longitude: float
    color: str
    phase: float
    state: str = "SECURE"
    crypto_state: str = "ML-KEM READY"
    command_state: str = "OPEN"
    transport: str = "lora"
    online: bool = True
    contact: str = "OPEN"
    link_margin: float = 75.0
    health: float = 92.0
    risk: float = 18.0
    temperature_c: float = 22.0
    battery_percent: float = 95.0
    bus_voltage: float = 4.92
    current_ma: float = 108.0
    packet_counter: int = 1200
    packet_rate: float = 1.1
    replay_rejects: int = 0
    queue_depth: int = 0
    satellites: int = 9
    hdop: float = 0.9
    altitude_m: float = 12.0
    speed_mps: float = 0.2
    course_deg: float = 0.0
    fix_age_ms: int = 250
    rssi_dbm: float = -78.0
    snr_db: float = 9.5
    gas_co2_ppm: float = 426.0
    gas_voc_ppb: float = 112.0
    magnet_ut: float = 44.0
    accel_g: float = 0.03
    gyro_dps: float = 0.08
    radiation_cpm: float = 18.0
    pressure_hpa: float = 1012.0
    humidity_percent: float = 48.0
    last_contact_s: float = 0.0
    last_fault: str = "none"
    telemetry_interval_s: float = 8.0
    target_interval_s: float = 8.0
    interval_mode: str = "AUTO"
    elapsed_since_tx_s: float = 0.0
    next_tx_s: float = 8.0
    classifier_label: str = "NOMINAL"
    classifier_score: float = 0.0
    anomaly_score: float = 0.0
    moving_link: float = 75.0
    moving_risk: float = 18.0
    moving_temp: float = 22.0
    cadence_reason: str = "nominal"
    radio_id: int = 0
    history: dict[str, deque] = field(default_factory=dict)


@dataclass
class CommandEntry:
    command_id: int
    node: str
    command: str
    route: str
    status: str
    detail: str
    created: str
    attempts: int = 0
    max_attempts: int = 0
    timeout_s: float = 0.0
    last_tx_s: float = 0.0
    acked: str = ""
    raw_hex: str = ""
    radio_id: int = 0


class MissionControlCore:
    def __init__(self, simulate: bool = True, rangepi_port: str | None = None, baud: int = 115200) -> None:
        self.simulate = simulate
        self.rangepi_port = rangepi_port
        self.baud = baud
        self.registry = NodeRegistry()
        self.export_dir = Path(__file__).resolve().parents[1] / "exports"
        self.lock = threading.RLock()
        self.running = False
        self.thread: threading.Thread | None = None
        self.rangepi_thread: threading.Thread | None = None
        self.rangepi_link: SerialLink | None = None
        self.rangepi_running = False
        self.rangepi_connected = False
        self.rangepi_last_rx_at = 0.0
        self.rangepi_rx_lines = 0
        self.rangepi_tx_lines = 0
        self.rangepi_rx_packets = 0
        self.rangepi_parse_errors = 0
        self.rangepi_bytes_rx = 0
        self.rangepi_bytes_tx = 0
        self.rangepi_recent_lines: deque[dict[str, Any]] = deque(maxlen=80)
        self.rangepi_lock = threading.RLock()
        self.rangepi_queue: queue.Queue[tuple[str, Any]] = queue.Queue()
        self.event_engine = EventEngine()
        self.hardware_nodes_by_src: dict[int, str] = {}
        self.command_timeout_s = 2.5
        self.command_max_attempts = 3
        self.started_at = time.time()
        self.session_dir = self.export_dir / "sessions"
        self.session_path = self.session_dir / f"{time.strftime('%Y%m%d_%H%M%S')}_mission.jsonl"
        self.timeline: deque[dict[str, Any]] = deque(maxlen=320)
        self.event_sequence = 1
        self.tick = 0
        self.selected = "SF-MISSION"
        self.command_target = "SELECTED"
        self.transport = "lora" if rangepi_port else "sim"
        self.mode = "SIM-LINK" if simulate else "LIVE"
        self.accepted_frames = 0
        self.replay_rejects = 0
        self.command_sequence = 1
        self.nodes = self._load_or_default_nodes()
        self.node_radio_ids: dict[str, int] = {}
        default_radio_ids = [1, 3, 4, 5, 6, 7, 8, 9]
        for index, node in enumerate(self.nodes):
            default_radio_id = default_radio_ids[index] if index < len(default_radio_ids) else self._next_available_radio_id()
            radio_id = node.radio_id or default_radio_id
            node.radio_id = radio_id
            self.node_radio_ids[node.name] = radio_id
        repaired_nodes = self._repair_radio_id_collisions()
        self.feed: deque[dict[str, Any]] = deque(maxlen=240)
        self.alerts: deque[dict[str, Any]] = deque(maxlen=120)
        self.packets: deque[dict[str, Any]] = deque(maxlen=240)
        self.crypto_rx: deque[dict[str, Any]] = deque(maxlen=240)
        self.commands: deque[CommandEntry] = deque(maxlen=160)
        self.audit: deque[dict[str, Any]] = deque(maxlen=180)
        self._seed_history()
        self._record_event("session_start", {"mode": self.mode, "rangepi_port": self.rangepi_port, "baud": self.baud})
        self._log("CUBESAT MASTER CONTROL web console online", "info")
        self._log("Browser UI attached to Python mission core", "good")
        if self.rangepi_port:
            self._log(f"RangePi route configured: {self.rangepi_port}@{self.baud}", "warn")
        if repaired_nodes:
            self._log(f"radio map repaired; removed duplicate staged nodes: {', '.join(repaired_nodes)}", "warn")
            self.save_registry(quiet=True)
        self._alert("Bring-up checklist ready; use command `bringup` or open the BRING-UP view", "info")

    def _default_nodes(self) -> list[WebNode]:
        return [
            WebNode("SF-MISSION", "mission-control", 0xA13C91E0, 37.77490, -122.41940, CYAN, 0.1, link_margin=91, health=96, risk=12, packet_counter=1240),
            WebNode("BERKELEY-LAB", "research-node", 0x42D9BB10, 37.87150, -122.27300, GREEN, 1.4, link_margin=78, health=89, risk=22, packet_counter=780),
            WebNode("OAKLAND-UPLINK", "uplink-relay", 0x93AF6721, 37.80440, -122.27120, AMBER, 2.2, link_margin=66, health=84, risk=35, packet_counter=640),
            WebNode("SANJOSE-EDGE", "edge-cache", 0x0FD21A90, 37.33820, -121.88630, RED, 3.0, link_margin=58, health=77, risk=44, packet_counter=530),
            WebNode("MARIN-WATCH", "field-sensor", 0x7E219A04, 37.97350, -122.53110, BLUE, 4.2, link_margin=74, health=86, risk=27, packet_counter=990),
        ]

    def _load_or_default_nodes(self) -> list[WebNode]:
        try:
            entries = self.registry.load()
        except Exception:
            entries = []
        if not entries:
            return self._default_nodes()
        nodes = [self._node_from_registry(entry, index) for index, entry in enumerate(entries)]
        return nodes or self._default_nodes()

    def _node_from_registry(self, data: dict[str, Any], index: int) -> WebNode:
        palette = [CYAN, GREEN, AMBER, RED, BLUE, MAGENTA]
        return WebNode(
            name=str(data.get("name", f"NODE-{index + 1:02d}")).upper(),
            role=str(data.get("node_role", data.get("role", "sensor-node"))),
            session_id=int(data.get("session_id", random.randint(0x10000000, 0xEFFFFFFF))) & 0xFFFFFFFF,
            latitude=float(data.get("fixed_latitude", data.get("latitude", 37.70 + index * 0.05))),
            longitude=float(data.get("fixed_longitude", data.get("longitude", -122.45 + index * 0.06))),
            color=str(data.get("color", palette[index % len(palette)])),
            phase=float(data.get("phase", index * 0.9)),
            state=str(data.get("state", "SECURE")),
            crypto_state=str(data.get("crypto_state", "ML-KEM READY")),
            command_state=str(data.get("command_state", "OPEN")),
            online=bool(data.get("online", True)),
            link_margin=float(data.get("link_margin", 70.0)),
            health=float(data.get("health", data.get("battery_percent", 90.0))),
            risk=float(data.get("replay_risk", data.get("risk", 20.0))),
            temperature_c=float(data.get("temperature_c", 22.0)),
            battery_percent=float(data.get("battery_percent", 92.0)),
            packet_counter=int(data.get("counter", data.get("packet_counter", 1))),
            satellites=int(data.get("satellites_seen", data.get("satellites", 8))),
            hdop=float(data.get("gnss_hdop", data.get("hdop", 1.0))),
            altitude_m=float(data.get("gnss_altitude_m", data.get("altitude_m", 10.0))),
            speed_mps=float(data.get("gnss_speed_mps", data.get("speed_mps", 0.1))),
            course_deg=float(data.get("gnss_course_deg", data.get("course_deg", 0.0))),
            fix_age_ms=int(data.get("gnss_fix_age_ms", data.get("fix_age_ms", 300))),
            telemetry_interval_s=float(data.get("telemetry_interval_s", 8.0)),
            target_interval_s=float(data.get("target_interval_s", 8.0)),
            interval_mode=str(data.get("interval_mode", "AUTO")).upper(),
            radio_id=int(data.get("radio_id", 0)) & 0xFF,
        )

    def _node_to_registry(self, node: WebNode) -> dict[str, Any]:
        return {
            "name": node.name,
            "node_role": node.role,
            "session_id": node.session_id,
            "color": node.color,
            "phase": node.phase,
            "state": node.state,
            "crypto_state": node.crypto_state,
            "command_state": node.command_state,
            "counter": node.packet_counter,
            "link_margin": node.link_margin,
            "satellites_seen": node.satellites,
            "temperature_c": node.temperature_c,
            "fixed_latitude": node.latitude,
            "fixed_longitude": node.longitude,
            "online": node.online,
            "battery_percent": node.battery_percent,
            "replay_risk": node.risk,
            "gnss_altitude_m": node.altitude_m,
            "gnss_hdop": node.hdop,
            "gnss_speed_mps": node.speed_mps,
            "gnss_course_deg": node.course_deg,
            "gnss_fix_age_ms": node.fix_age_ms,
            "telemetry_interval_s": node.telemetry_interval_s,
            "target_interval_s": node.target_interval_s,
            "interval_mode": node.interval_mode,
            "radio_id": self._node_radio_id(node),
        }

    def _seed_history(self) -> None:
        for node in self.nodes:
            self._init_history_for_node(node)

    def _init_history_for_node(self, node: WebNode, samples: int = 72) -> None:
        node.history = {key: deque(maxlen=180) for key in ("link", "temp", "risk", "packets", "battery", "snr", "interval", "anomaly", "tx")}
        for step in range(samples):
            wave = math.sin((step / 8.0) + node.phase)
            node.history["link"].append(max(0, min(100, node.link_margin + wave * 4)))
            node.history["temp"].append(node.temperature_c + wave * 0.6)
            node.history["risk"].append(max(0, min(100, node.risk + wave * 3)))
            node.history["packets"].append(max(0, node.packet_rate + wave * 0.15))
            node.history["battery"].append(max(0, min(100, node.battery_percent - (samples - step) * 0.012)))
            node.history["snr"].append(node.snr_db + wave * 0.5)
            node.history["interval"].append(node.telemetry_interval_s)
            node.history["anomaly"].append(node.anomaly_score)
            node.history["tx"].append(0)

    def start(self) -> None:
        if self.running:
            return
        self.running = True
        self._start_rangepi_reader()
        self.thread = threading.Thread(target=self._loop, name="web-mastercontrol-core", daemon=True)
        self.thread.start()

    def stop(self) -> None:
        self.running = False
        self._stop_rangepi_reader()
        if self.thread is not None:
            self.thread.join(timeout=1.5)
        self.save_registry(quiet=True)

    def _loop(self) -> None:
        while self.running:
            time.sleep(0.45)
            with self.lock:
                self._drain_rangepi_queue()
                self._process_command_retries()
                if self.simulate:
                    self._update_simulation()

    def _record_event(self, kind: str, payload: dict[str, Any]) -> None:
        event = {
            "id": self.event_sequence,
            "time": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "clock": _now_label(),
            "kind": kind,
            "node": str(payload.get("node", payload.get("target", ""))),
            "severity": self._timeline_severity(kind, payload),
            "summary": self._timeline_summary(kind, payload),
            "payload": payload,
        }
        self.event_sequence += 1
        self.timeline.append(event)
        try:
            self.session_dir.mkdir(parents=True, exist_ok=True)
            with self.session_path.open("a", encoding="utf-8") as handle:
                json.dump(event, handle, separators=(",", ":"), sort_keys=True)
                handle.write("\n")
        except Exception:
            return

    def _timeline_severity(self, kind: str, payload: dict[str, Any]) -> str:
        severity = str(payload.get("severity", "")).lower()
        state = str(payload.get("state", payload.get("status", payload.get("result", "")))).lower()
        if severity in {"bad", "fail", "error"} or state in {"failed", "rejected", "fail"}:
            return "bad"
        if severity in {"warn", "command"} or kind in {"command_retry", "command_failed"} or state in {"warn", "retrying", "pending"}:
            return "warn"
        return "info"

    def _timeline_summary(self, kind: str, payload: dict[str, Any]) -> str:
        if kind == "session_start":
            return f"mission recorder started mode={payload.get('mode')} route={payload.get('rangepi_port') or 'sim'}"
        if kind == "log":
            return str(payload.get("message", "log event"))
        if kind == "alert":
            return str(payload.get("message", "alert"))
        if kind == "packet":
            return (
                f"{payload.get('route', 'route')} {payload.get('type', 'packet')} "
                f"{payload.get('node', '')} counter={payload.get('counter', '-')}"
            )
        if kind == "command":
            return (
                f"command #{payload.get('command_id')} {payload.get('command')} "
                f"to {payload.get('node')} status={payload.get('status')}"
            )
        if kind == "command_ack":
            return f"ACK #{payload.get('command_id')} {payload.get('node')} {payload.get('detail', '')}"
        if kind == "command_retry":
            return f"retry #{payload.get('command_id')} attempt={payload.get('attempts')}/{payload.get('max_attempts')}"
        if kind == "command_failed":
            return f"failed #{payload.get('command_id')} {payload.get('detail', '')}"
        if kind == "node_add":
            return f"node added {payload.get('node')} radio_id={payload.get('radio_id')} session={payload.get('session')}"
        if kind == "node_delete":
            return f"node deleted {payload.get('node')} radio_id={payload.get('radio_id')}"
        return kind.replace("_", " ")

    def _start_rangepi_reader(self) -> None:
        if not self.rangepi_port or self.rangepi_running:
            return
        self.rangepi_running = True
        self.rangepi_thread = threading.Thread(target=self._rangepi_reader_loop, name="web-rangepi-reader", daemon=True)
        self.rangepi_thread.start()

    def _stop_rangepi_reader(self) -> None:
        self.rangepi_running = False
        with self.rangepi_lock:
            link = self.rangepi_link
            self.rangepi_link = None
            self.rangepi_connected = False
        if link is not None:
            try:
                link.close()
            except Exception:
                pass
        if self.rangepi_thread is not None:
            self.rangepi_thread.join(timeout=1.5)
            self.rangepi_thread = None

    def _rangepi_reader_loop(self) -> None:
        link: SerialLink | None = None
        try:
            link = SerialLink(self.rangepi_port or "", baudrate=self.baud, timeout=0.2)
            link.open()
            with self.rangepi_lock:
                self.rangepi_link = link
                self.rangepi_connected = True
            self.rangepi_queue.put(("status", f"RangePi connected: {self.rangepi_port}@{self.baud}"))
            while self.rangepi_running:
                line = link.read_line()
                if line:
                    self.rangepi_queue.put(("line", line))
        except Exception as exc:
            self.rangepi_queue.put(("error", str(exc)))
        finally:
            with self.rangepi_lock:
                if self.rangepi_link is link:
                    self.rangepi_link = None
                self.rangepi_connected = False
            if link is not None:
                try:
                    link.close()
                except Exception:
                    pass
            self.rangepi_queue.put(("closed", "RangePi link closed"))

    def _drain_rangepi_queue(self) -> None:
        while True:
            try:
                kind, payload = self.rangepi_queue.get_nowait()
            except queue.Empty:
                break
            if kind == "status":
                self.transport = "rangepi"
                self.mode = "LIVE+SIM" if self.simulate else "LIVE"
                self._log(str(payload), "good")
            elif kind == "closed":
                self._log(str(payload), "warn")
            elif kind == "error":
                self.rangepi_parse_errors += 1
                self._log(f"RangePi error: {payload}", "bad")
            elif kind == "line" and isinstance(payload, bytes):
                self._handle_rangepi_line(payload)

    def _history_average(self, node: WebNode, key: str, window: int = 18) -> float:
        values = list(node.history.get(key, []))[-window:]
        if not values:
            return 0.0
        return sum(float(value) for value in values) / len(values)

    def _classify_node(self, node: WebNode) -> tuple[str, float, float, str]:
        link_stress = max(0.0, 62.0 - node.link_margin) * 1.45
        gnss_stress = max(0.0, node.hdop - 1.6) * 18.0 + max(0.0, node.fix_age_ms - 1800) / 55.0
        sensor_stress = max(0.0, node.temperature_c - 32.0) * 3.0 + max(0.0, node.radiation_cpm - 34.0) * 1.2
        power_stress = max(0.0, 28.0 - node.battery_percent) * 2.6
        anomaly = max(0.0, min(100.0, node.risk * 0.42 + link_stress * 0.24 + gnss_stress * 0.18 + sensor_stress * 0.1 + power_stress * 0.06))

        if node.interval_mode == "FAST":
            return "MANUAL-FAST", anomaly, 2.0, "operator override"
        if node.interval_mode == "SLOW":
            return "MANUAL-SLOW", anomaly, 18.0, "operator override"
        if node.interval_mode == "NORMAL":
            return "NOMINAL", anomaly, 8.0, "operator override"

        if node.battery_percent < 18.0:
            return "CONSERVE", anomaly, 24.0, "battery reserve"
        if anomaly >= 68.0 or node.last_fault != "none":
            return "PRIORITY", anomaly, 2.5, node.last_fault if node.last_fault != "none" else "anomaly score"
        if anomaly >= 44.0 or node.link_margin < 55.0:
            return "WATCH", anomaly, 5.0, "trend elevated"
        if node.contact == "LIMITED" and anomaly < 28.0:
            return "LOW-RATE", anomaly, 14.0, "limited contact"
        return "NOMINAL", anomaly, 8.0, "moving averages nominal"

    def _update_simulation(self) -> None:
        self.tick += 1
        now = time.time()
        for node in self.nodes:
            wave = math.sin(now / 8.0 + node.phase)
            fast = math.sin(now / 2.7 + node.phase * 1.8)
            contact_open = (self.tick + int(node.phase * 31)) % 140 < 104
            node.contact = "OPEN" if contact_open else "LIMITED"
            node.link_margin = max(22, min(98, node.link_margin + fast * 0.35 + (0.08 if contact_open else -0.22)))
            node.health = max(42, min(100, (node.battery_percent * 0.45) + (node.link_margin * 0.35) + (100 - node.risk) * 0.2))
            node.risk = max(5, min(88, node.risk + (0.08 if not contact_open else -0.05) + fast * 0.11))
            node.temperature_c = max(12, min(45, node.temperature_c + wave * 0.03))
            node.battery_percent = max(8, min(100, node.battery_percent - 0.002 + (0.008 if contact_open and node.role == "mission-control" else 0)))
            node.bus_voltage = 3.65 + node.battery_percent * 0.014
            node.current_ma = 95 + (100 - node.link_margin) * 2.4 + abs(fast) * 22
            node.satellites = max(3, min(12, int(8 + math.sin(now / 11 + node.phase) * 3)))
            node.hdop = max(0.65, min(4.2, 2.5 - node.satellites * 0.13 + abs(fast) * 0.28))
            node.fix_age_ms = int(180 + (0 if contact_open else 1800) + abs(wave) * 620)
            node.altitude_m = max(-4, node.altitude_m + wave * 0.01)
            node.speed_mps = max(0.0, 0.2 + abs(fast) * 0.42)
            node.course_deg = (node.course_deg + fast * 0.6) % 360
            node.rssi_dbm = -112 + node.link_margin * 0.46 + wave * 2.0
            node.snr_db = -2 + node.link_margin * 0.15 + fast
            node.gas_co2_ppm = max(380, min(760, node.gas_co2_ppm + fast * 0.7))
            node.gas_voc_ppb = max(40, min(480, node.gas_voc_ppb + wave * 1.2))
            node.magnet_ut = max(22, min(70, node.magnet_ut + fast * 0.08))
            node.accel_g = max(0.0, min(0.28, 0.02 + abs(fast) * 0.06))
            node.gyro_dps = max(0.0, min(0.32, 0.04 + abs(wave) * 0.08))
            node.radiation_cpm = max(8, min(90, node.radiation_cpm + fast * 0.15))
            node.pressure_hpa = max(965, min(1030, node.pressure_hpa + wave * 0.04))
            node.humidity_percent = max(18, min(88, node.humidity_percent + fast * 0.05))
            node.last_contact_s = 0 if contact_open else node.last_contact_s + 0.45
            node.last_fault = self._fault_for_node(node)

            previous_classifier = node.classifier_label
            node.moving_link = self._history_average(node, "link", 18) or node.link_margin
            node.moving_risk = self._history_average(node, "risk", 18) or node.risk
            node.moving_temp = self._history_average(node, "temp", 18) or node.temperature_c
            label, anomaly, target_interval, reason = self._classify_node(node)
            node.classifier_label = label
            node.anomaly_score = anomaly
            node.classifier_score = anomaly / 100.0
            node.target_interval_s = target_interval
            node.cadence_reason = reason
            node.telemetry_interval_s += (node.target_interval_s - node.telemetry_interval_s) * 0.08
            node.telemetry_interval_s = max(1.5, min(30.0, node.telemetry_interval_s))
            node.elapsed_since_tx_s += 0.45
            tx_sent = 0
            if node.online and node.elapsed_since_tx_s >= node.telemetry_interval_s:
                node.packet_counter += 1
                node.elapsed_since_tx_s = 0.0
                node.next_tx_s = node.telemetry_interval_s
                tx_sent = 1
                self.accepted_frames += 1
                self._packet(node, "adaptive-telemetry", "accepted")
                self._log(
                    f"RX {node.name} adaptive counter={node.packet_counter} "
                    f"interval={node.telemetry_interval_s:.1f}s class={node.classifier_label} "
                    f"ma_link={node.moving_link:.0f}% anomaly={node.anomaly_score:.0f}",
                    "info",
                )
            else:
                node.next_tx_s = max(0.0, node.telemetry_interval_s - node.elapsed_since_tx_s)
            node.packet_rate = 1.0 / max(1.0, node.telemetry_interval_s)

            if previous_classifier != node.classifier_label and self.tick > 2:
                severity = "warn" if node.classifier_label in {"PRIORITY", "WATCH", "CONSERVE"} else "info"
                self._log(
                    f"CLASSIFIER {node.name} {previous_classifier}->{node.classifier_label} "
                    f"target={node.target_interval_s:.1f}s reason={node.cadence_reason}",
                    severity,
                )
            for key, value in (
                ("link", node.link_margin),
                ("temp", node.temperature_c),
                ("risk", node.risk),
                ("packets", node.packet_rate),
                ("battery", node.battery_percent),
                ("snr", node.snr_db),
                ("interval", node.telemetry_interval_s),
                ("anomaly", node.anomaly_score),
                ("tx", tx_sent),
            ):
                node.history[key].append(round(value, 3))

        if self.tick % 37 == 0:
            candidate = max(self.nodes, key=lambda item: item.risk)
            if candidate.risk > 42:
                self._alert(f"{candidate.name} risk trend elevated: {candidate.risk:.0f}%", "warn")

    def _fault_for_node(self, node: WebNode) -> str:
        if node.fix_age_ms > 3000:
            return "gnss stale"
        if node.link_margin < 45:
            return "rf margin weak"
        if node.risk > 70:
            return "security risk high"
        return "none"

    def _selected_node(self) -> WebNode:
        return self._find_node(self.selected) or self.nodes[0]

    def _find_node(self, name: str) -> WebNode | None:
        name = name.upper()
        for node in self.nodes:
            if node.name == name:
                return node
        return None

    def _target_nodes(self) -> list[WebNode]:
        if self.command_target == "ALL":
            return list(self.nodes)
        if self.command_target == "SELECTED":
            return [self._selected_node()]
        node = self._find_node(self.command_target)
        return [node] if node is not None else [self._selected_node()]

    def _log(self, message: str, severity: str = "info") -> None:
        entry = {"time": _now_label(), "severity": severity, "message": message}
        self.feed.append(entry)
        self._record_event("log", entry)

    def _alert(self, message: str, severity: str = "warn") -> None:
        entry = {"time": _now_label(), "severity": severity, "message": message, "status": "open"}
        self.alerts.append(entry)
        self._record_event("alert", entry)
        self._log(f"ALERT {message}", severity)

    def _packet(self, node: WebNode, packet_type: str, status: str) -> None:
        raw_seed = f"{node.name}|{packet_type}|{node.session_id:08X}|{node.packet_counter}|{status}".encode("utf-8")
        raw_hex = hashlib.blake2s(raw_seed, digest_size=32).hexdigest().upper()
        packet = {
            "time": _now_label(),
            "node": node.name,
            "type": packet_type,
            "counter": node.packet_counter,
            "session": f"0x{node.session_id:08X}",
            "status": status,
            "route": self.transport,
            "raw_hex": raw_hex,
        }
        self.packets.append(packet)
        self._record_event("packet", packet)
        self.crypto_rx.append(self._crypto_frame_for_packet(node, packet_type, status))

    def _crypto_frame_for_packet(self, node: WebNode, packet_type: str, status: str) -> dict[str, Any]:
        seed = (
            f"{node.name}|{node.session_id:08x}|{node.packet_counter}|{packet_type}|"
            f"{node.anomaly_score:.2f}|{self.tick}|{self.transport}"
        ).encode("utf-8")
        digest = hashlib.sha256(seed).digest() + hashlib.sha256(seed + b":body").digest()
        header = bytes(
            [
                0xC5,
                0x01,
                len(packet_type) & 0xFF,
                node.packet_counter & 0xFF,
            ]
        ) + node.session_id.to_bytes(4, "big") + node.packet_counter.to_bytes(4, "big")
        nonce = hashlib.blake2s(seed, digest_size=12).digest()
        ciphertext = bytes(byte ^ 0xA5 for byte in digest[:48])
        tag = hashlib.blake2s(header + nonce + ciphertext, digest_size=16).digest()
        replay_ok = status != "rejected"
        auth_ok = status == "accepted"
        return {
            "time": _now_label(),
            "node": node.name,
            "route": self.transport,
            "packet_type": packet_type,
            "session": f"0x{node.session_id:08X}",
            "counter": node.packet_counter,
            "algorithm": "ML-KEM session / ML-DSA auth / AEAD demo",
            "classifier": node.classifier_label,
            "cadence_s": round(node.telemetry_interval_s, 2),
            "header_hex": header.hex().upper(),
            "nonce_hex": nonce.hex().upper(),
            "ciphertext_hex": ciphertext.hex().upper(),
            "tag_hex": tag.hex().upper(),
            "auth": "PASS" if auth_ok else "FAIL",
            "replay": "PASS" if replay_ok else "REJECT",
            "status": status,
            "rssi_dbm": round(node.rssi_dbm, 1),
            "snr_db": round(node.snr_db, 2),
        }

    def _packet_type_name(self, packet_type: int) -> str:
        names = {
            HOPE_PACKET_TYPE_TELEMETRY: "telemetry",
            HOPE_PACKET_TYPE_ACK: "ack",
            HOPE_PACKET_TYPE_DIAGNOSTIC: "diagnostic",
            HOPE_PACKET_TYPE_HANDSHAKE: "handshake",
            HOPE_PACKET_TYPE_COMMAND: "command",
        }
        return names.get(packet_type, f"type-{packet_type}")

    def _node_radio_id(self, node: WebNode) -> int:
        if node.radio_id:
            self.node_radio_ids[node.name] = node.radio_id
            return node.radio_id
        if node.name in self.node_radio_ids:
            node.radio_id = self.node_radio_ids[node.name]
            return node.radio_id
        radio_id = max(self.node_radio_ids.values(), default=0) + 1
        radio_id = max(1, min(255, radio_id))
        self.node_radio_ids[node.name] = radio_id
        node.radio_id = radio_id
        return radio_id

    def _next_available_radio_id(self, used: set[int] | None = None) -> int:
        reserved = set(self.node_radio_ids.values())
        if used:
            reserved.update(used)
        for radio_id in range(1, 256):
            if radio_id not in reserved:
                return radio_id
        return 255

    def _repair_radio_id_collisions(self) -> list[str]:
        kept: list[WebNode] = []
        removed: list[str] = []
        assigned: set[int] = set()
        for node in self.nodes:
            radio_id = int(node.radio_id or self.node_radio_ids.get(node.name, 0))
            if radio_id < 1 or radio_id > 255:
                radio_id = 0
            if radio_id and radio_id not in assigned:
                node.radio_id = radio_id
                assigned.add(radio_id)
                kept.append(node)
                continue
            if node.name.startswith("ESP32-SRC") and radio_id:
                removed.append(node.name)
                continue
            node.radio_id = self._next_available_radio_id(assigned)
            assigned.add(node.radio_id)
            kept.append(node)
        self.nodes = kept
        self.node_radio_ids = {node.name: node.radio_id for node in self.nodes}
        return removed

    def _find_node_by_session(self, session_id: int) -> WebNode | None:
        for node in self.nodes:
            if node.session_id == session_id:
                return node
        return None

    def _find_node_by_radio_id(self, radio_id: int) -> WebNode | None:
        for node in self.nodes:
            if self._node_radio_id(node) == radio_id:
                return node
        return None

    def _node_for_packet(self, packet: HopePacket) -> WebNode:
        node = self._find_node_by_session(packet.session_id)
        if node is None:
            name = self.hardware_nodes_by_src.get(packet.src_id)
            node = self._find_node(name) if name else None
        if node is None:
            node = self._find_node_by_radio_id(packet.src_id)
        if node is not None:
            self.hardware_nodes_by_src[packet.src_id] = node.name
            self.node_radio_ids[node.name] = packet.src_id
            node.radio_id = packet.src_id
            return node

        index = len(self.nodes)
        node = WebNode(
            name=f"ESP32-SRC{packet.src_id:02X}",
            role="rangepi-downlink",
            session_id=packet.session_id,
            latitude=37.7749 + math.sin(index) * 0.04,
            longitude=-122.4194 + math.cos(index) * 0.04,
            color=[CYAN, GREEN, AMBER, RED, BLUE, MAGENTA][index % 6],
            phase=random.random() * math.tau,
            state="SECURE",
            crypto_state="WIRE PACKET",
            transport="rangepi",
            packet_counter=packet.counter,
            radio_id=packet.src_id,
        )
        self._init_history_for_node(node)
        self.nodes.append(node)
        self.hardware_nodes_by_src[packet.src_id] = node.name
        self.node_radio_ids[node.name] = packet.src_id
        node.radio_id = packet.src_id
        self._alert(f"new RangePi source discovered: {node.name} src={packet.src_id}", "info")
        return node

    def _append_history_sample(self, node: WebNode, tx_sent: int = 1) -> None:
        if not node.history:
            self._init_history_for_node(node, samples=1)
        for key, value in (
            ("link", node.link_margin),
            ("temp", node.temperature_c),
            ("risk", node.risk),
            ("packets", node.packet_rate),
            ("battery", node.battery_percent),
            ("snr", node.snr_db),
            ("interval", node.telemetry_interval_s),
            ("anomaly", node.anomaly_score),
            ("tx", tx_sent),
        ):
            node.history[key].append(round(value, 3))

    def _record_wire_crypto_frame(self, raw: bytes, packet: HopePacket, node: WebNode, status: str) -> None:
        header = raw[:HOPE_HEADER_LEN]
        body = raw[HOPE_HEADER_LEN:]
        nonce = hashlib.blake2s(raw + b":nonce", digest_size=12).digest()
        tag = hashlib.blake2s(raw + b":tag", digest_size=16).digest()
        self.crypto_rx.append(
            {
                "time": _now_label(),
                "node": node.name,
                "route": "rangepi",
                "packet_type": self._packet_type_name(packet.packet_type),
                "session": f"0x{packet.session_id:08X}",
                "counter": packet.counter,
                "algorithm": "HOPE wire packet / lattice auth metadata",
                "classifier": node.classifier_label,
                "cadence_s": round(node.telemetry_interval_s, 2),
                "header_hex": header.hex().upper(),
                "nonce_hex": nonce.hex().upper(),
                "ciphertext_hex": body.hex().upper() or raw.hex().upper(),
                "tag_hex": tag.hex().upper(),
                "auth": "PASS" if status == "accepted" else "FAIL",
                "replay": "PASS" if status == "accepted" else "REJECT",
                "status": status,
                "rssi_dbm": round(node.rssi_dbm, 1),
                "snr_db": round(node.snr_db, 2),
            }
        )

    def _record_wire_packet(self, raw: bytes, packet: HopePacket, node: WebNode, status: str) -> None:
        packet_type = self._packet_type_name(packet.packet_type)
        entry = {
            "time": _now_label(),
            "node": node.name,
            "type": packet_type,
            "counter": packet.counter,
            "session": f"0x{packet.session_id:08X}",
            "status": status,
            "route": "rangepi",
            "raw_hex": raw.hex().upper(),
        }
        self.packets.append(entry)
        self._record_event("packet", entry)
        self._record_wire_crypto_frame(raw, packet, node, status)

    def _handle_rangepi_line(self, line: bytes) -> None:
        self.rangepi_rx_lines += 1
        self.rangepi_bytes_rx += len(line)
        self.rangepi_last_rx_at = time.time()
        self.rangepi_recent_lines.append({"time": _now_label(), "direction": "RX", "text": line[:160].decode("utf-8", errors="replace")})
        try:
            raw_packet = parse_rangepi_line(line)
            result = self.event_engine.handle_raw_packet(raw_packet)
            packet: HopePacket = result["packet"]
            node = self._node_for_packet(packet)
            self.rangepi_rx_packets += 1
            status = "accepted" if result["accepted"] else "rejected"
            self._record_wire_packet(raw_packet, packet, node, status)
            if result["accepted"]:
                self.accepted_frames += 1
                self._apply_wire_packet_to_node(node, packet)
            else:
                self.replay_rejects += 1
                node.replay_rejects += 1
                node.risk = min(100.0, node.risk + 6.0)
            self._log(format_packet_result(result), "info" if result["accepted"] else "bad")
        except Exception as exc:
            self.rangepi_parse_errors += 1
            preview = line[:80].decode("utf-8", errors="replace")
            self._log(f"RangePi line ignored: {preview} ({exc})", "muted")

    def _apply_wire_packet_to_node(self, node: WebNode, packet: HopePacket) -> None:
        node.online = True
        node.transport = "rangepi"
        node.session_id = packet.session_id
        node.packet_counter = packet.counter
        node.last_contact_s = 0.0

        if packet.packet_type == HOPE_PACKET_TYPE_TELEMETRY:
            telemetry = parse_telemetry_payload(packet.payload)
            node.latitude = telemetry.latitude
            node.longitude = telemetry.longitude
            node.temperature_c = telemetry.temperature_c
            node.satellites = telemetry.satellites
            node.hdop = telemetry.hdop
            node.altitude_m = telemetry.altitude_m
            node.speed_mps = telemetry.speed_mps
            node.course_deg = telemetry.course_deg
            node.fix_age_ms = telemetry.fix_age_ms
            node.link_margin = max(20.0, min(98.0, 96.0 - max(0.0, telemetry.hdop - 0.7) * 18.0 + telemetry.satellites * 1.4))
            node.rssi_dbm = -112.0 + node.link_margin * 0.48
            node.snr_db = -3.0 + node.link_margin * 0.16
            node.risk = max(5.0, min(90.0, node.risk + (4.0 if telemetry.fix_age_ms > 1500 else -1.0)))
            node.contact = "OPEN"
            node.state = "SECURE"
            node.packet_rate = 1.0 / max(1.0, node.telemetry_interval_s)
            label, anomaly, target_interval, reason = self._classify_node(node)
            node.classifier_label = label
            node.anomaly_score = anomaly
            node.classifier_score = anomaly / 100.0
            node.target_interval_s = target_interval
            node.cadence_reason = reason
            self._append_history_sample(node, tx_sent=1)
            return

        if packet.packet_type == HOPE_PACKET_TYPE_ACK:
            ack = parse_ack_payload(packet.payload)
            for entry in reversed(self.commands):
                if entry.command_id == ack.command_id and entry.node == node.name:
                    ack_name = ack_status_name(ack.status)
                    entry.status = "acked" if ack_name == "ok" else ack_name
                    entry.detail = ack.message or f"detail={ack.detail_code}"
                    entry.acked = _now_label()
                    self._record_event("command_ack", entry.__dict__)
                    break
            self._append_history_sample(node, tx_sent=0)
            return

        if packet.packet_type == HOPE_PACKET_TYPE_DIAGNOSTIC:
            diagnostic = parse_diagnostic_payload(packet.payload)
            status_name = diagnostic_status_name(diagnostic.overall_status)
            node.last_fault = "none" if status_name == "PASS" else ",".join(diagnostic_mask_names(diagnostic.warning_mask | diagnostic.failed_mask)) or status_name.lower()
            node.risk = max(5.0, min(95.0, node.risk + (8.0 if status_name == "FAIL" else 2.0 if status_name == "WARN" else -2.0)))
            node.health = max(30.0, min(100.0, node.health - (12.0 if status_name == "FAIL" else 3.0 if status_name == "WARN" else -1.0)))
            self._append_history_sample(node, tx_sent=0)
            return

        if packet.packet_type == HOPE_PACKET_TYPE_HANDSHAKE:
            fragment = parse_fragment_payload(packet.payload)
            node.crypto_state = f"LATTICE {message_name(fragment.message_type)}"
            self._append_history_sample(node, tx_sent=0)

    def _command(
        self,
        node: WebNode,
        command: str,
        status: str = "acked",
        detail: str = "",
        *,
        route: str | None = None,
        attempts: int = 0,
        max_attempts: int = 0,
        timeout_s: float = 0.0,
        last_tx_s: float = 0.0,
        raw_hex: str = "",
        radio_id: int = 0,
    ) -> CommandEntry:
        entry = CommandEntry(
            command_id=self.command_sequence,
            node=node.name,
            command=command,
            route=route or self.transport,
            status=status,
            detail=detail or "accepted by simulated node",
            created=_now_label(),
            attempts=attempts,
            max_attempts=max_attempts,
            timeout_s=timeout_s,
            last_tx_s=last_tx_s,
            raw_hex=raw_hex,
            radio_id=radio_id,
        )
        self.command_sequence += 1
        self.commands.append(entry)
        self._record_event("command", entry.__dict__)
        self.audit.append(
            {
                "time": entry.created,
                "actor": "operator",
                "target": node.name,
                "action": command,
                "result": status,
            }
        )
        return entry

    def execute(self, command: str) -> dict[str, Any]:
        with self.lock:
            command = command.strip()
            if not command:
                return {"ok": False, "lines": ["empty command"]}
            self._log(f"master[{self.command_target}]> {command}", "command")
            parts = command.split()
            lower = [part.lower() for part in parts]
            lines: list[str] = []

            if lower[0] == "help":
                lines.append("commands: use <all|selected|node|#> | tx <cmd> [node|all] | rx <latest|node|clear> | rf | queue | record | nodes | status | classify | cadence <auto|fast|normal|slow> | ping | selftest | telemetry | pause | resume | transport <lora|wifi|auto> | pair <kem|pin|manual> | addnode <name> <role> [lat=] [lon=] [radio=] [session=] | delnode <name> | replay | isolate | connect | downlink | arm | rotate | clear | save | export all | bringup")
            elif lower[0] == "clear":
                self.feed.clear()
                lines.append("terminal buffer cleared")
            elif lower[0] in {"tx", "send", "cmd"}:
                lines.extend(self._tx_command(parts[1:]))
            elif lower[0] in {"rx", "recv", "receive"}:
                lines.extend(self._rx_command(parts[1:]))
            elif lower[0] in {"rf", "radio", "rangepi"}:
                lines.extend(self._rf_command())
            elif lower[0] in {"queue", "acks", "ack"}:
                lines.extend(self._queue_command())
            elif lower[0] in {"record", "recorder", "session"}:
                lines.append(f"mission recorder: {self.session_path}")
            elif lower[0] in {"use", "target", "select"}:
                lines.extend(self._set_target(parts[1:]))
            elif lower[0] in {"nodes", "fleet"}:
                for index, node in enumerate(self.nodes, start=1):
                    marker = "*" if node.name == self._selected_node().name else " "
                    lines.append(
                        f"{marker} {index}: {node.name} role={node.role} radio={self._node_radio_id(node)} "
                        f"link={node.link_margin:.0f}% session=0x{node.session_id:08X}"
                    )
            elif lower[0] == "status":
                for node in self._target_nodes():
                    lines.append(
                        f"{node.name}: {node.state} contact={node.contact} link={node.link_margin:.0f}% "
                        f"gnss={node.satellites} sats hdop={node.hdop:.2f} "
                        f"class={node.classifier_label} interval={node.telemetry_interval_s:.1f}s crypto={node.crypto_state}"
                    )
            elif lower[0] in {"classify", "classifier"}:
                for node in self._target_nodes():
                    lines.append(
                        f"{node.name}: class={node.classifier_label} score={node.anomaly_score:.0f} "
                        f"target={node.target_interval_s:.1f}s next={node.next_tx_s:.1f}s reason={node.cadence_reason}"
                    )
            elif lower[0] in {"cadence", "rate", "interval"}:
                mode = lower[1].upper() if len(lower) > 1 else "AUTO"
                if mode not in {"AUTO", "FAST", "NORMAL", "SLOW"}:
                    lines.append("cadence must be auto, fast, normal, or slow")
                else:
                    for node in self._target_nodes():
                        node.interval_mode = mode
                        label, anomaly, target_interval, reason = self._classify_node(node)
                        node.classifier_label = label
                        node.anomaly_score = anomaly
                        node.target_interval_s = target_interval
                        node.cadence_reason = reason
                        self._command(node, f"cadence-{mode.lower()}", "acked", f"target interval {target_interval:.1f}s")
                        lines.append(f"{node.name}: cadence={mode} target={target_interval:.1f}s reason={reason}")
                    if self._rangepi_is_connected():
                        lines.extend(self._send_hardware_command("cadence", self._target_nodes(), arg=mode.lower()))
                    self.save_registry(quiet=True)
            elif lower[0] in {"ping", "selftest", "telemetry", "telemetry-now", "pause", "resume", "arm", "isolate", "connect", "downlink", "rotate"}:
                lines.extend(self._apply_node_command(lower[0]))
            elif lower[0] in {"transport", "link"}:
                mode = lower[1] if len(lower) > 1 else "auto"
                if mode not in {"lora", "wifi", "auto", "sim"}:
                    lines.append("transport must be lora, wifi, auto, or sim")
                else:
                    self.transport = mode
                    for node in self._target_nodes():
                        node.transport = mode
                        self._command(node, f"transport-{mode}", "acked", f"route changed to {mode}")
                    if self._rangepi_is_connected():
                        lines.extend(self._send_hardware_command("transport", self._target_nodes(), arg=mode))
                    lines.append(f"ground transport route={mode}")
            elif lower[0] == "pair":
                method = lower[1] if len(lower) > 1 else "kem"
                for node in self._target_nodes():
                    node.crypto_state = "ML-KEM READY" if method == "kem" else f"PAIRED-{method.upper()}"
                    node.session_id = random.randint(0x10000000, 0xEFFFFFFF)
                    self._command(node, f"pair-{method}", "acked", f"session=0x{node.session_id:08X}")
                    lines.append(f"{node.name} paired via {method}; session=0x{node.session_id:08X}")
                self.save_registry(quiet=True)
            elif lower[0] == "replay":
                node = self._selected_node()
                self.replay_rejects += 1
                node.replay_rejects += 1
                node.risk = min(100, node.risk + 8)
                self._packet(node, "replay", "rejected")
                self._alert(f"{node.name} replay rejected counter={node.packet_counter}", "bad")
                lines.append(f"replay rejected for {node.name}")
            elif lower[0] == "addnode":
                lines.extend(self._add_node(parts[1:]))
            elif lower[0] == "delnode":
                lines.extend(self._delete_node(parts[1:]))
            elif lower[0] == "save":
                self.save_registry()
                lines.append(f"registry saved: {self.registry.path}")
            elif lower[0] == "export":
                kind = lower[1] if len(lower) > 1 else "all"
                paths = self.export(kind)
                lines.append(f"exported {len(paths)} file(s) to {self.export_dir}")
            elif lower[0] == "bringup":
                lines.extend([f"{item['state']} {item['name']}: {item['detail']}" for item in self.bringup_checks()])
            else:
                lines.append(f"unknown command: {command}")

            for line in lines:
                self._log(line, "good" if not line.lower().startswith(("unknown", "transport must")) else "warn")
            return {"ok": True, "lines": lines, "state": self.snapshot()}

    def _nodes_for_spec(self, spec: str | None) -> list[WebNode]:
        if not spec:
            return self._target_nodes()
        value = spec.upper()
        if value == "ALL":
            return list(self.nodes)
        if value == "SELECTED":
            return [self._selected_node()]
        if value.isdigit():
            index = max(1, min(len(self.nodes), int(value))) - 1
            return [self.nodes[index]]
        node = self._find_node(value)
        return [node] if node is not None else []

    def _rangepi_is_connected(self) -> bool:
        with self.rangepi_lock:
            return self.rangepi_connected and self.rangepi_link is not None

    def _write_rangepi_tx(self, raw: bytes, label: str) -> None:
        with self.rangepi_lock:
            link = self.rangepi_link
            if not self.rangepi_connected or link is None:
                raise RuntimeError("serial link is not connected")
            frame = b"TX " + raw.hex().upper().encode("ascii")
            link.write_line(frame)
        self.rangepi_tx_lines += 1
        self.rangepi_bytes_tx += len(frame) + 1
        self.rangepi_recent_lines.append({"time": _now_label(), "direction": "TX", "text": label})

    def _process_command_retries(self) -> None:
        if not self._rangepi_is_connected():
            return
        now = time.time()
        for entry in list(self.commands):
            if entry.route != "rangepi" or entry.status not in {"pending", "sent", "retrying"}:
                continue
            if not entry.raw_hex or entry.max_attempts <= 0 or entry.timeout_s <= 0:
                continue
            if now - entry.last_tx_s < entry.timeout_s:
                continue
            if entry.attempts >= entry.max_attempts:
                entry.status = "failed"
                entry.detail = f"ACK timeout after {entry.attempts} attempts"
                self._record_event("command_failed", entry.__dict__)
                self._alert(f"{entry.node} command #{entry.command_id} timed out", "warn")
                continue
            try:
                raw = bytes.fromhex(entry.raw_hex)
                entry.attempts += 1
                entry.last_tx_s = now
                entry.status = "retrying"
                self._write_rangepi_tx(raw, f"RETRY #{entry.command_id} {entry.command} attempt={entry.attempts}")
                self._log(f"RETRY {entry.node} #{entry.command_id} {entry.command} attempt={entry.attempts}/{entry.max_attempts}", "warn")
                self._record_event("command_retry", entry.__dict__)
            except Exception as exc:
                entry.status = "failed"
                entry.detail = f"retry failed: {exc}"
                self._record_event("command_failed", entry.__dict__)

    def _send_hardware_command(self, command: str, nodes: list[WebNode], arg: str | bytes = b"") -> list[str]:
        try:
            opcode = command_opcode_from_name(command)
        except ValueError as exc:
            return [str(exc)]

        lines: list[str] = []
        for node in nodes:
            command_id = self.command_sequence
            radio_id = self._node_radio_id(node)
            raw = encode_command_packet(
                command_id=command_id,
                opcode=opcode,
                counter=command_id,
                session_id=node.session_id,
                timestamp=int(time.time()),
                src_id=2,
                dst_id=radio_id,
                arg=arg,
            )
            try:
                self._write_rangepi_tx(raw, f"TX #{command_id} {command} dst={radio_id} bytes={len(raw)}")
            except Exception as exc:
                self._command(node, command, "failed", str(exc), route="rangepi", radio_id=radio_id)
                lines.append(f"{node.name}: TX failed: {exc}")
                continue
            self._command(
                node,
                command,
                "pending",
                f"waiting for ACK; RangePi dst={radio_id} bytes={len(raw)}",
                route="rangepi",
                attempts=1,
                max_attempts=self.command_max_attempts,
                timeout_s=self.command_timeout_s,
                last_tx_s=time.time(),
                raw_hex=raw.hex().upper(),
                radio_id=radio_id,
            )
            packet_entry = {
                "time": _now_label(),
                "node": node.name,
                "type": f"tx-{command}",
                "counter": command_id,
                "session": f"0x{node.session_id:08X}",
                "status": "sent",
                "route": "rangepi",
                "raw_hex": raw.hex().upper(),
            }
            self.packets.append(packet_entry)
            self._record_event("packet", packet_entry)
            lines.append(
                f"{node.name}: TX {command} sent over RangePi dst={radio_id} "
                f"counter={command_id} attempt=1/{self.command_max_attempts}"
            )
        return lines

    def _tx_command(self, args: list[str]) -> list[str]:
        if not args:
            return ["usage: tx <ping|selftest|telemetry|pause|resume|downlink|rotate|arm|isolate|connect> [node|all]"]
        allowed = {"ping", "selftest", "telemetry", "telemetry-now", "pause", "resume", "downlink", "rotate", "arm", "isolate", "connect"}
        first = args[0].lower()
        if first in allowed:
            command = first
            target = args[1] if len(args) > 1 else None
        elif len(args) > 1 and args[1].lower() in allowed:
            target = args[0]
            command = args[1].lower()
        else:
            return ["usage: tx <ping|selftest|telemetry|pause|resume|downlink|rotate|arm|isolate|connect> [node|all]"]
        nodes = self._nodes_for_spec(target)
        if not nodes:
            return [f"tx target not found: {target}"]
        return self._apply_node_command(command, nodes)

    def _rx_command(self, args: list[str]) -> list[str]:
        if args and args[0].lower() == "clear":
            self.crypto_rx.clear()
            self.packets.clear()
            return ["rx buffers cleared"]
        frames = list(self.crypto_rx)
        if args and args[0].lower() == "node" and len(args) > 1:
            wanted = args[1].upper()
            frames = [frame for frame in frames if frame["node"].upper() == wanted]
        elif args and args[0].lower() not in {"latest", "last"}:
            wanted = args[0].upper()
            node_filtered = [frame for frame in frames if frame["node"].upper() == wanted]
            if node_filtered:
                frames = node_filtered
        if not frames:
            return ["rx buffer empty"]
        if args and args[0].lower() in {"latest", "last"}:
            latest = frames[-1]
            return [
                f"rx latest {latest['node']} {latest['packet_type']} counter={latest['counter']} "
                f"auth={latest['auth']} replay={latest['replay']} session={latest['session']}",
                f"nonce={latest['nonce_hex']}",
                f"ciphertext={latest['ciphertext_hex'][:96]}...",
                f"tag={latest['tag_hex']}",
            ]
        return [
            f"{frame['time']} {frame['node']} {frame['packet_type']} counter={frame['counter']} "
            f"auth={frame['auth']} replay={frame['replay']} cipher={frame['ciphertext_hex'][:32]}..."
            for frame in frames[-8:]
        ]

    def _rf_command(self) -> list[str]:
        age = time.time() - self.rangepi_last_rx_at if self.rangepi_last_rx_at else None
        packet_rate = self._rangepi_packet_rate()
        return [
            f"rangepi connected={self.rangepi_connected} port={self.rangepi_port or 'none'} baud={self.baud}",
            f"rx_lines={self.rangepi_rx_lines} rx_packets={self.rangepi_rx_packets} tx_lines={self.rangepi_tx_lines} parse_errors={self.rangepi_parse_errors}",
            f"bytes rx={self.rangepi_bytes_rx} tx={self.rangepi_bytes_tx} packet_rate={packet_rate:.2f}/s last_rx_age={age:.1f}s" if age is not None else f"bytes rx={self.rangepi_bytes_rx} tx={self.rangepi_bytes_tx} packet_rate={packet_rate:.2f}/s last_rx_age=never",
        ]

    def _rangepi_packet_rate(self, window_s: float = 30.0) -> float:
        recent = 0
        for packet in self.packets:
            if packet.get("route") != "rangepi":
                continue
            # Packet rows store wall-clock labels, so use the command/RX counters
            # as a coarse dashboard rate over the active process lifetime.
            recent += 1
        uptime = max(1.0, time.time() - self.started_at)
        return min(float(recent) / min(uptime, window_s), 99.0)

    def _queue_command(self) -> list[str]:
        commands = list(self.commands)[-12:]
        if not commands:
            return ["command queue empty"]
        lines = []
        for entry in reversed(commands):
            age = max(0.0, time.time() - entry.last_tx_s) if entry.last_tx_s else 0.0
            lines.append(
                f"#{entry.command_id} {entry.node} {entry.command} status={entry.status} "
                f"attempts={entry.attempts}/{entry.max_attempts} age={age:.1f}s detail={entry.detail}"
            )
        return lines

    def _set_target(self, args: list[str]) -> list[str]:
        if not args:
            return [f"target={self.command_target} selected={self.selected}"]
        value = args[0].upper()
        if value == "ALL":
            self.command_target = "ALL"
            return ["command target set to ALL"]
        if value == "SELECTED":
            self.command_target = "SELECTED"
            return [f"command target follows selected node {self.selected}"]
        if value.isdigit():
            index = max(1, min(len(self.nodes), int(value))) - 1
            self.selected = self.nodes[index].name
            self.command_target = "SELECTED"
            return [f"selected {self.selected}"]
        node = self._find_node(value)
        if node is None:
            return [f"node not found: {value}"]
        self.selected = node.name
        self.command_target = node.name
        return [f"selected and targeted {node.name}"]

    def _apply_node_command(self, command: str, nodes: list[WebNode] | None = None) -> list[str]:
        lines: list[str] = []
        target_nodes = nodes if nodes is not None else self._target_nodes()
        if self._rangepi_is_connected():
            return self._send_hardware_command(command, target_nodes)
        for node in target_nodes:
            if command == "ping":
                self.accepted_frames += 1
                self._packet(node, "ping", "accepted")
                detail = f"telemetry counter={node.packet_counter} link={node.link_margin:.0f}%"
            elif command == "selftest":
                detail = f"selftest pass temp={node.temperature_c:.1f}C gnss={node.satellites} rf={node.link_margin:.0f}%"
            elif command == "isolate":
                node.command_state = "ISOLATED"
                node.state = "QUARANTINE"
                node.risk = min(100, node.risk + 5)
                detail = "node isolated"
            elif command == "connect":
                node.command_state = "OPEN"
                node.state = "SECURE"
                node.online = True
                detail = "node connected"
            elif command == "downlink":
                node.state = "DOWNLINK"
                detail = "downlink window opened"
            elif command == "telemetry" or command == "telemetry-now":
                self.accepted_frames += 1
                self._packet(node, "telemetry-now", "accepted")
                detail = f"telemetry queued counter={node.packet_counter}"
            elif command == "pause":
                node.command_state = "PAUSED"
                detail = "telemetry paused"
            elif command == "resume":
                node.command_state = "OPEN"
                detail = "telemetry resumed"
            elif command == "arm":
                node.state = "ARMED"
                detail = "command lane armed"
            elif command == "rotate":
                node.session_id = random.randint(0x10000000, 0xEFFFFFFF)
                node.crypto_state = "ML-KEM READY"
                detail = f"session rotated to 0x{node.session_id:08X}"
            else:
                detail = "accepted"
            self._command(node, command, "acked", detail)
            lines.append(f"{node.name}: {detail}")
        self.save_registry(quiet=True)
        return lines

    def _split_admin_args(self, args: list[str]) -> tuple[list[str], dict[str, str]]:
        positional: list[str] = []
        options: dict[str, str] = {}
        for arg in args:
            if "=" in arg:
                key, value = arg.split("=", 1)
                options[key.strip().lower()] = value.strip()
            else:
                positional.append(arg)
        return positional, options

    def _normalize_node_name(self, value: str) -> str:
        cleaned = re.sub(r"[^A-Z0-9_-]+", "-", value.strip().upper()).strip("-_")
        return cleaned[:32]

    def _parse_float_field(self, value: str | None, default: float, label: str, low: float, high: float) -> tuple[float | None, str | None]:
        if value is None or value == "":
            return default, None
        try:
            parsed = float(value)
        except ValueError:
            return None, f"{label} must be numeric"
        if parsed < low or parsed > high:
            return None, f"{label} must be between {low:g} and {high:g}"
        return parsed, None

    def _parse_int_field(self, value: str | None, default: int, label: str, low: int, high: int) -> tuple[int | None, str | None]:
        if value is None or value == "":
            return default, None
        try:
            parsed = int(value, 0)
        except ValueError:
            return None, f"{label} must be an integer"
        if parsed < low or parsed > high:
            return None, f"{label} must be between {low} and {high}"
        return parsed, None

    def _add_node(self, args: list[str]) -> list[str]:
        positional, options = self._split_admin_args(args)
        if len(positional) < 2:
            return ["usage: addnode <name> <role> [lat=37.77] [lon=-122.42] [radio=7] [session=0x12345678]"]
        name = self._normalize_node_name(positional[0])
        if not name:
            return ["node name must include letters or numbers"]
        if self._find_node(name) is not None:
            return [f"node already exists: {name}"]
        role = positional[1].lower()
        index = len(self.nodes)
        default_lat = 37.69 + index * 0.035
        default_lon = -122.49 + index * 0.045
        lat_text = options.get("lat", options.get("latitude", positional[2] if len(positional) > 2 else ""))
        lon_text = options.get("lon", options.get("longitude", positional[3] if len(positional) > 3 else ""))
        radio_text = options.get("radio", options.get("radio_id", positional[4] if len(positional) > 4 else ""))
        session_text = options.get("session", options.get("session_id", positional[5] if len(positional) > 5 else ""))
        latitude, error = self._parse_float_field(lat_text, default_lat, "lat", -90.0, 90.0)
        if error:
            return [error]
        longitude, error = self._parse_float_field(lon_text, default_lon, "lon", -180.0, 180.0)
        if error:
            return [error]
        radio_id, error = self._parse_int_field(radio_text, max(self.node_radio_ids.values(), default=0) + 1, "radio", 1, 255)
        if error:
            return [error]
        assert radio_id is not None
        assigned = {value for key, value in self.node_radio_ids.items() if key != name}
        if radio_id in assigned:
            owner = next((key for key, value in self.node_radio_ids.items() if value == radio_id), "unknown")
            return [f"radio id {radio_id} already assigned to {owner}"]
        session_id, error = self._parse_int_field(session_text, random.randint(0x10000000, 0xEFFFFFFF), "session", 1, 0xFFFFFFFF)
        if error:
            return [error]
        assert latitude is not None and longitude is not None and session_id is not None
        palette = [CYAN, GREEN, AMBER, RED, BLUE, MAGENTA]
        color = options.get("color", palette[index % len(palette)])
        if not re.fullmatch(r"#[0-9A-Fa-f]{6}", color):
            color = palette[index % len(palette)]
        node = WebNode(
            name=name,
            role=role,
            session_id=session_id & 0xFFFFFFFF,
            latitude=latitude,
            longitude=longitude,
            color=color,
            phase=random.random() * math.tau,
            crypto_state="UNPAIRED",
            command_state="PAIRING",
            link_margin=52,
            risk=48,
            satellites=6,
            radio_id=radio_id,
        )
        self.nodes.append(node)
        self._init_history_for_node(node)
        self.node_radio_ids[node.name] = radio_id
        self.selected = node.name
        self.command_target = node.name
        self.save_registry(quiet=True)
        self._alert(f"node staged for pairing: {node.name} radio={radio_id}", "warn")
        self._record_event(
            "node_add",
            {
                "node": node.name,
                "role": role,
                "radio_id": radio_id,
                "session": f"0x{node.session_id:08X}",
                "latitude": round(latitude, 6),
                "longitude": round(longitude, 6),
                "state": "staged",
            },
        )
        return [
            (
                f"added {node.name} role={role} radio={radio_id} "
                f"lat={latitude:.5f} lon={longitude:.5f} session=0x{node.session_id:08X}; run pair kem"
            )
        ]

    def _delete_node(self, args: list[str]) -> list[str]:
        if not args:
            return ["usage: delnode <name>"]
        name = args[0].upper()
        if len(self.nodes) <= 1:
            return ["cannot delete the last node"]
        for index, node in enumerate(self.nodes):
            if node.name == name:
                radio_id = self.node_radio_ids.pop(node.name, node.radio_id)
                for src_id, mapped_name in list(self.hardware_nodes_by_src.items()):
                    if mapped_name == node.name:
                        del self.hardware_nodes_by_src[src_id]
                del self.nodes[index]
                self.selected = self.nodes[0].name
                self.command_target = "SELECTED"
                self.save_registry(quiet=True)
                self._alert(f"node deleted: {name}", "warn")
                self._record_event("node_delete", {"node": name, "radio_id": radio_id, "state": "removed"})
                return [f"deleted {name} radio={radio_id}"]
        return [f"node not found: {name}"]

    def save_registry(self, quiet: bool = False) -> None:
        self.registry.save([self._node_to_registry(node) for node in self.nodes])
        if not quiet:
            self._log(f"registry saved: {self.registry.path}", "good")

    def _write_csv(self, name: str, headers: list[str], rows: list[tuple[Any, ...]]) -> Path:
        self.export_dir.mkdir(parents=True, exist_ok=True)
        path = self.export_dir / f"{time.strftime('%Y%m%d_%H%M%S')}_{name}.csv"
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(headers)
            writer.writerows(rows)
        return path

    def _write_json(self, name: str, payload: Any) -> Path:
        self.export_dir.mkdir(parents=True, exist_ok=True)
        path = self.export_dir / f"{time.strftime('%Y%m%d_%H%M%S')}_{name}.json"
        with path.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, sort_keys=True)
            handle.write("\n")
        return path

    def export(self, kind: str = "all") -> list[Path]:
        kind = kind.lower()
        paths: list[Path] = []
        if kind in {"all", "nodes"}:
            paths.append(self._write_json("nodes", [self._node_to_registry(node) for node in self.nodes]))
        if kind in {"all", "commands", "queue"}:
            paths.append(
                self._write_csv(
                    "commands",
                    ["id", "node", "command", "route", "status", "detail", "created"],
                    [(item.command_id, item.node, item.command, item.route, item.status, item.detail, item.created) for item in self.commands],
                )
            )
        if kind in {"all", "packets"}:
            paths.append(
                self._write_csv(
                    "packets",
                    ["time", "node", "type", "counter", "session", "status", "route"],
                    [(p["time"], p["node"], p["type"], p["counter"], p["session"], p["status"], p["route"]) for p in self.packets],
                )
            )
        if kind in {"all", "alerts"}:
            paths.append(self._write_json("alerts", list(self.alerts)))
        if kind in {"all", "summary"}:
            paths.append(self._write_json("summary", self.snapshot(include_history=False)))
        return paths

    def bringup_checks(self) -> list[dict[str, str]]:
        repo_root = Path(__file__).resolve().parents[2]
        firmware_bin = repo_root / "firmware" / "build" / "cubesat_firmware.bin"
        compile_commands = repo_root / "firmware" / "build" / "compile_commands.json"
        sdkconfig_defaults = repo_root / "firmware" / "sdkconfig.defaults"
        build_script = repo_root / "deploy" / "esp_build.ps1"
        flash_script = repo_root / "deploy" / "esp_flash_monitor.ps1"
        idf_exports = [Path("C:/esp/v6.0.1/esp-idf/export.ps1"), Path("C:/esp/v6.0/esp-idf/export.ps1")]
        idf_export = next((path for path in idf_exports if path.exists()), None)
        selected = self._selected_node()
        bin_detail = str(firmware_bin)
        if firmware_bin.exists():
            bin_detail = f"{firmware_bin} ({firmware_bin.stat().st_size:,} bytes)"
        checks = [
            ("ESP-IDF export", "PASS" if idf_export else "WARN", str(idf_export or "C:/esp/v6.0.1/esp-idf/export.ps1 not found")),
            ("ESP-IDF build", "PASS" if firmware_bin.exists() else "FAIL", bin_detail),
            ("IntelliSense database", "PASS" if compile_commands.exists() else "WARN", str(compile_commands)),
            ("Pin/default config", "PASS" if sdkconfig_defaults.exists() else "FAIL", str(sdkconfig_defaults)),
            ("Build helper", "PASS" if build_script.exists() else "FAIL", str(build_script)),
            ("Flash helper", "PASS" if flash_script.exists() else "FAIL", str(flash_script)),
            ("Node registry", "PASS" if self.registry.path.exists() else "WARN", str(self.registry.path)),
            ("Node radio map", "PASS" if len(set(self.node_radio_ids.values())) == len(self.node_radio_ids) else "FAIL", f"{len(self.node_radio_ids)} ids assigned"),
            ("RangePi route", "PASS" if self.rangepi_connected else "WARN", self.rangepi_port or "simulated/no serial"),
            ("Selected node", "PASS" if selected.online else "FAIL", f"{selected.name} {selected.state}"),
            ("GNSS quality", "PASS" if selected.satellites >= 7 and selected.hdop < 2.0 else "WARN", f"{selected.satellites} sats hdop={selected.hdop:.2f} age={selected.fix_age_ms}ms"),
            ("RF margin", "PASS" if selected.link_margin >= 55 else "WARN", f"{selected.link_margin:.0f}% {self.transport}"),
            ("Adaptive cadence", "PASS" if selected.classifier_label not in {"PRIORITY", "CONSERVE"} else "WARN", f"{selected.classifier_label} interval={selected.telemetry_interval_s:.1f}s reason={selected.cadence_reason}"),
            ("Replay guard", "PASS" if self.replay_rejects == 0 else "WARN", f"rejects={self.replay_rejects}"),
            ("PQ lane", "PASS" if "KEM" in selected.crypto_state else "WARN", selected.crypto_state),
        ]
        return [{"name": name, "state": state, "detail": detail} for name, state, detail in checks]

    def security_lifecycle(self) -> list[dict[str, str]]:
        node = self._selected_node()
        latest_crypto = list(self.crypto_rx)[-1] if self.crypto_rx else {}
        return [
            {
                "name": "Packet Framing",
                "state": "PASS" if self.packets else "WARN",
                "detail": f"{len(self.packets)} packet rows; selected session=0x{node.session_id:08X}",
            },
            {
                "name": "Replay Guard",
                "state": "PASS" if self.replay_rejects == 0 else "WARN",
                "detail": f"strict counters per session; rejects={self.replay_rejects}",
            },
            {
                "name": "ML-KEM Session",
                "state": "PASS" if "KEM" in node.crypto_state or "LATTICE" in node.crypto_state else "WARN",
                "detail": node.crypto_state,
            },
            {
                "name": "Command ACK",
                "state": "PASS" if not [item for item in self.commands if item.status in {"pending", "retrying", "failed"}] else "WARN",
                "detail": f"pending={len([item for item in self.commands if item.status in {'pending', 'retrying'}])} failed={len([item for item in self.commands if item.status == 'failed'])}",
            },
            {
                "name": "Encrypted RX View",
                "state": "PASS" if latest_crypto else "WARN",
                "detail": f"{latest_crypto.get('node', 'none')} {latest_crypto.get('packet_type', 'no frames')}",
            },
            {
                "name": "Mission Recorder",
                "state": "PASS" if self.session_path.exists() else "WARN",
                "detail": str(self.session_path),
            },
        ]

    def packet_flow_state(self) -> list[dict[str, str]]:
        pending = len([item for item in self.commands if item.status in {"pending", "retrying"}])
        failed = len([item for item in self.commands if item.status == "failed"])
        return [
            {
                "name": "ESP32 Node",
                "state": "PASS" if self.nodes else "WARN",
                "detail": f"{len([node for node in self.nodes if node.online])}/{len(self.nodes)} online",
            },
            {
                "name": "SX1262 / LoRa",
                "state": "PASS" if self.rangepi_connected or self.simulate else "WARN",
                "detail": self.transport,
            },
            {
                "name": "RangePi Bridge",
                "state": "PASS" if self.rangepi_connected else "WARN",
                "detail": f"{self.rangepi_port or 'not configured'} rx_lines={self.rangepi_rx_lines}",
            },
            {
                "name": "Packet Parser",
                "state": "PASS" if self.rangepi_rx_packets > 0 and self.rangepi_parse_errors == 0 else "WARN",
                "detail": f"packets={self.rangepi_rx_packets} parse_errors={self.rangepi_parse_errors}",
            },
            {
                "name": "Replay Filter",
                "state": "PASS" if self.replay_rejects == 0 else "WARN",
                "detail": f"rejects={self.replay_rejects}",
            },
            {
                "name": "Crypto/Auth",
                "state": "PASS" if self.crypto_rx else "WARN",
                "detail": f"rx_frames={len(self.crypto_rx)}",
            },
            {
                "name": "Command ACK",
                "state": "PASS" if pending == 0 and failed == 0 else "WARN" if pending else "FAIL",
                "detail": f"pending={pending} failed={failed}",
            },
            {
                "name": "UI / Recorder",
                "state": "PASS" if self.session_path.exists() else "WARN",
                "detail": self.session_path.name,
            },
        ]

    def snapshot(self, include_history: bool = True) -> dict[str, Any]:
        nodes = [self._node_snapshot(node, include_history) for node in self.nodes]
        avg_link = sum(node.link_margin for node in self.nodes) / max(1, len(self.nodes))
        avg_health = sum(node.health for node in self.nodes) / max(1, len(self.nodes))
        avg_risk = sum(node.risk for node in self.nodes) / max(1, len(self.nodes))
        avg_interval = sum(node.telemetry_interval_s for node in self.nodes) / max(1, len(self.nodes))
        open_alerts = len([item for item in self.alerts if item["status"] == "open"])
        pending_commands = len([item for item in self.commands if item.status in {"pending", "sent", "retrying"}])
        failed_commands = len([item for item in self.commands if item.status == "failed"])
        return {
            "time": _now_label(),
            "uptime_s": int(time.time() - self.started_at),
            "tick": self.tick,
            "mode": self.mode,
            "transport": self.transport,
            "selected": self.selected,
            "target": self.command_target,
            "hardware": {
                "rangepi_port": self.rangepi_port,
                "rangepi_baud": self.baud,
                "rangepi_connected": self.rangepi_connected,
                "rangepi_rx_lines": self.rangepi_rx_lines,
                "rangepi_tx_lines": self.rangepi_tx_lines,
                "rangepi_rx_packets": self.rangepi_rx_packets,
                "rangepi_parse_errors": self.rangepi_parse_errors,
                "rangepi_bytes_rx": self.rangepi_bytes_rx,
                "rangepi_bytes_tx": self.rangepi_bytes_tx,
                "rangepi_packet_rate": round(self._rangepi_packet_rate(), 2),
                "rangepi_last_rx_age_s": round(time.time() - self.rangepi_last_rx_at, 1) if self.rangepi_last_rx_at else None,
                "recent_lines": list(self.rangepi_recent_lines)[-40:],
            },
            "recorder": {
                "session_path": str(self.session_path),
            },
            "security": self.security_lifecycle(),
            "flow": self.packet_flow_state(),
            "stats": {
                "accepted_frames": self.accepted_frames,
                "replay_rejects": self.replay_rejects,
                "avg_link": round(avg_link, 1),
                "avg_health": round(avg_health, 1),
                "avg_risk": round(avg_risk, 1),
                "open_alerts": open_alerts,
                "nodes_online": len([node for node in self.nodes if node.online]),
                "lora_channels": len([node for node in self.nodes if node.transport in {"lora", "sim", "rangepi"}]),
                "avg_interval_s": round(avg_interval, 1),
                "priority_nodes": len([node for node in self.nodes if node.classifier_label in {"PRIORITY", "WATCH", "MANUAL-FAST"}]),
                "pending_commands": pending_commands,
                "failed_commands": failed_commands,
            },
            "nodes": nodes,
            "selected_node": self._node_snapshot(self._selected_node(), include_history),
            "feed": list(self.feed)[-120:],
            "alerts": list(self.alerts)[-80:],
            "packets": list(self.packets)[-120:],
            "crypto_rx": list(self.crypto_rx)[-160:],
            "commands": [item.__dict__ for item in list(self.commands)[-100:]],
            "timeline": list(self.timeline)[-180:],
            "audit": list(self.audit)[-100:],
            "bringup": self.bringup_checks(),
        }

    def _moving_series(self, values: list[float], window: int = 12) -> list[float]:
        output: list[float] = []
        for index in range(len(values)):
            start = max(0, index - window + 1)
            segment = values[start : index + 1]
            output.append(round(sum(segment) / len(segment), 3))
        return output

    def _node_snapshot(self, node: WebNode, include_history: bool = True) -> dict[str, Any]:
        drift_lat = node.latitude + math.sin(time.time() / 24.0 + node.phase) * 0.00065
        drift_lon = node.longitude + math.cos(time.time() / 26.0 + node.phase) * 0.00065
        payload = {
            "name": node.name,
            "role": node.role,
            "session": f"0x{node.session_id:08X}",
            "session_id": node.session_id,
            "radio_id": self._node_radio_id(node),
            "latitude": node.latitude,
            "longitude": node.longitude,
            "map_latitude": drift_lat,
            "map_longitude": drift_lon,
            "color": node.color,
            "state": node.state,
            "crypto_state": node.crypto_state,
            "command_state": node.command_state,
            "transport": node.transport,
            "online": node.online,
            "contact": node.contact,
            "link_margin": round(node.link_margin, 1),
            "health": round(node.health, 1),
            "risk": round(node.risk, 1),
            "temperature_c": round(node.temperature_c, 2),
            "battery_percent": round(node.battery_percent, 1),
            "bus_voltage": round(node.bus_voltage, 3),
            "current_ma": round(node.current_ma, 1),
            "packet_counter": node.packet_counter,
            "packet_rate": round(node.packet_rate, 2),
            "telemetry_interval_s": round(node.telemetry_interval_s, 2),
            "target_interval_s": round(node.target_interval_s, 2),
            "next_tx_s": round(node.next_tx_s, 2),
            "interval_mode": node.interval_mode,
            "classifier_label": node.classifier_label,
            "classifier_score": round(node.classifier_score, 3),
            "anomaly_score": round(node.anomaly_score, 1),
            "moving_link": round(node.moving_link, 1),
            "moving_risk": round(node.moving_risk, 1),
            "moving_temp": round(node.moving_temp, 2),
            "cadence_reason": node.cadence_reason,
            "replay_rejects": node.replay_rejects,
            "queue_depth": node.queue_depth,
            "satellites": node.satellites,
            "hdop": round(node.hdop, 2),
            "altitude_m": round(node.altitude_m, 2),
            "speed_mps": round(node.speed_mps, 2),
            "course_deg": round(node.course_deg, 1),
            "fix_age_ms": node.fix_age_ms,
            "rssi_dbm": round(node.rssi_dbm, 1),
            "snr_db": round(node.snr_db, 2),
            "gas_co2_ppm": round(node.gas_co2_ppm, 1),
            "gas_voc_ppb": round(node.gas_voc_ppb, 1),
            "magnet_ut": round(node.magnet_ut, 2),
            "accel_g": round(node.accel_g, 3),
            "gyro_dps": round(node.gyro_dps, 3),
            "radiation_cpm": round(node.radiation_cpm, 1),
            "pressure_hpa": round(node.pressure_hpa, 1),
            "humidity_percent": round(node.humidity_percent, 1),
            "last_contact_s": round(node.last_contact_s, 1),
            "last_fault": node.last_fault,
        }
        if include_history:
            history = {key: list(value) for key, value in node.history.items()}
            payload["history"] = history
            payload["history_ma"] = {
                key: self._moving_series([float(item) for item in values], 12)
                for key, values in history.items()
                if key in {"link", "temp", "risk", "packets", "battery", "snr", "interval", "anomaly"}
            }
        return payload


class MastercontrolHandler(BaseHTTPRequestHandler):
    core: MissionControlCore
    asset_dir: Path

    def log_message(self, format: str, *args: Any) -> None:
        return

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/api/state":
            self._json(self.core.snapshot())
            return
        if parsed.path == "/api/health":
            self._json({"ok": True, "mode": self.core.mode})
            return
        if parsed.path == "/api/export":
            query = parse_qs(parsed.query)
            kind = query.get("kind", ["all"])[0]
            with self.core.lock:
                paths = self.core.export(kind)
            self._json({"ok": True, "paths": [str(path) for path in paths]})
            return
        self._static(parsed.path)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            payload = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            payload = {}
        if parsed.path == "/api/command":
            result = self.core.execute(str(payload.get("command", "")))
            self._json(result)
            return
        self._json({"ok": False, "error": "not found"}, status=404)

    def _json(self, payload: Any, status: int = 200) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _static(self, path: str) -> None:
        if path in {"", "/"}:
            path = "/index.html"
        relative = path.lstrip("/").replace("/", os.sep)
        target = (self.asset_dir / relative).resolve()
        if not str(target).startswith(str(self.asset_dir.resolve())) or not target.exists():
            self.send_error(404)
            return
        content_type = {
            ".html": "text/html; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".js": "application/javascript; charset=utf-8",
            ".json": "application/json; charset=utf-8",
            ".svg": "image/svg+xml",
        }.get(target.suffix, "application/octet-stream")
        body = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def _server_class(core: MissionControlCore, asset_dir: Path) -> type[MastercontrolHandler]:
    class Handler(MastercontrolHandler):
        pass

    Handler.core = core
    Handler.asset_dir = asset_dir
    return Handler


def _bind_server(host: str, port: int, handler: type[BaseHTTPRequestHandler]) -> ThreadingHTTPServer:
    for candidate in range(port, port + 20):
        try:
            return ThreadingHTTPServer((host, candidate), handler)
        except OSError:
            continue
    raise OSError(f"no available port in range {port}-{port + 19}")


def main() -> None:
    parser = argparse.ArgumentParser(description="CubeSat Master Control web UI")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-browser", action="store_true")
    parser.add_argument("--no-sim", action="store_true", help="Accepted for launcher compatibility; simulated web data remains available.")
    parser.add_argument("--rangepi-port", help="RangePi serial port label, e.g. COM5 or sim://cubesat")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    asset_dir = Path(__file__).with_name("web")
    core = MissionControlCore(simulate=not args.no_sim or (args.rangepi_port or "").startswith("sim://"), rangepi_port=args.rangepi_port, baud=args.baud)
    handler = _server_class(core, asset_dir)
    server = _bind_server(args.host, args.port, handler)
    actual_port = server.server_address[1]
    url = f"http://{args.host}:{actual_port}/"

    core.start()
    print(f"CubeSat Master Control web UI: {url}")
    print("Press Ctrl+C to stop.")
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.shutdown()
        server.server_close()
        core.stop()


if __name__ == "__main__":
    main()
