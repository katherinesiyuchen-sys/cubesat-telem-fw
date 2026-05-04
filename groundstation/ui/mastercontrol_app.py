import argparse
import math
import os
import queue
import random
import threading
import time
import tkinter as tk
import webbrowser
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from tkinter import ttk
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from groundstation.backend.event_engine import EventEngine
from groundstation.backend.packet_parser import encode_telemetry_packet
from groundstation.backend.rangepi import parse_rangepi_line
from groundstation.backend.serial_link import SerialLink
from groundstation.models.telemetry import parse_telemetry_payload


BG = "#020203"
PANEL = "#050607"
PANEL_DARK = "#000000"
CYAN = "#8bdde8"
GREEN = "#9effb1"
AMBER = "#e6c36a"
MAGENTA = "#c59cff"
RED = "#ff6678"
TEXT = "#eef7f8"
MUTED = "#9aa3a6"
WHITE_MAP = "#d6dddf"
MAP_DARK = "#010101"
LINE = "#20262a"
OSM_TILE_URL = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
TILE_SIZE = 256
HOPE_PACKET_TYPE_TELEMETRY = 1


WORLD_POLYGONS = [
    [(-168, 72), (-140, 70), (-124, 58), (-122, 49), (-102, 50), (-82, 45), (-67, 48), (-52, 58), (-66, 66), (-96, 73), (-132, 74)],
    [(-126, 49), (-112, 34), (-105, 25), (-96, 18), (-87, 15), (-81, 25), (-76, 36), (-84, 45), (-102, 50)],
    [(-82, 12), (-74, 8), (-70, -6), (-78, -18), (-74, -34), (-66, -48), (-58, -54), (-48, -38), (-40, -18), (-50, -4), (-62, 6)],
    [(-18, 35), (4, 37), (24, 32), (34, 16), (32, -2), (24, -18), (18, -34), (10, -35), (0, -22), (-8, -4), (-16, 12)],
    [(-10, 72), (18, 70), (40, 60), (42, 48), (28, 40), (8, 42), (-8, 52)],
    [(34, 32), (52, 28), (76, 22), (92, 8), (104, -2), (116, 8), (122, 24), (108, 40), (82, 50), (58, 54), (40, 46)],
    [(70, 58), (104, 62), (138, 55), (156, 48), (146, 34), (116, 30), (88, 38), (62, 48)],
    [(112, -10), (138, -12), (154, -26), (146, -40), (126, -42), (112, -30)],
    [(-52, 62), (-36, 74), (-18, 70), (-22, 58), (-42, 55)],
]


@dataclass
class Satellite:
    name: str
    session_id: int
    color: str
    phase: float
    speed: float
    state: str
    crypto_state: str
    counter: int
    link_margin: float
    satellites_seen: int
    temperature_c: float
    contact_period: int
    contact_duration: int
    fixed_latitude: float
    fixed_longitude: float
    node_role: str
    command_state: str = "SECURE"
    online: bool = True
    last_contact_tick: int = 0
    history: dict[str, deque] | None = None
    battery_percent: float = 92.0
    bus_voltage: float = 3.8
    current_ma: float = 120.0
    packet_loss_percent: float = 1.2
    replay_risk: float = 3.0
    radiation_cpm: float = 18.0
    pressure_hpa: float = 1012.0
    humidity_percent: float = 42.0
    firmware_version: str = "0.1.0"
    boot_count: int = 1
    last_fault: str = "none"


@dataclass
class EnvironmentState:
    radiation: float = 18.0
    ionosphere: float = 22.0
    ground_weather: float = 12.0
    eclipse: bool = False
    operator_load: float = 34.0

    @property
    def risk(self) -> float:
        eclipse_penalty = 14.0 if self.eclipse else 0.0
        return min(100.0, (self.radiation * 0.34) + (self.ionosphere * 0.28) + (self.ground_weather * 0.24) + eclipse_penalty)


@dataclass
class CommandRecord:
    command_id: int
    node_name: str
    command: str
    status: str
    created_tick: int
    updated_tick: int
    detail: str


@dataclass
class AlertRecord:
    alert_id: int
    severity: str
    message: str
    created_tick: int
    status: str = "open"


@dataclass
class AuditRecord:
    audit_id: int
    category: str
    actor: str
    target: str
    action: str
    result: str
    tick: int


class FirmwareApi:
    """
    Python-facing boundary for the firmware protocol.

    Today this uses the Python packet codec that mirrors the C structs. When the
    C firmware/security code is compiled into a host library or test executable,
    this class is the place to swap in ctypes/subprocess calls.
    """

    def __init__(self) -> None:
        self.engine = EventEngine()

    def submit_telemetry(
        self,
        sat: Satellite,
        latitude: float,
        longitude: float,
        *,
        counter: int | None = None,
        note: str = "fresh",
    ) -> dict:
        raw = encode_telemetry_packet(
            counter=counter if counter is not None else sat.counter,
            latitude=latitude,
            longitude=longitude,
            temperature_c=sat.temperature_c,
            fix_type=3,
            satellites=sat.satellites_seen,
            session_id=sat.session_id,
            timestamp=int(time.time()),
        )
        result = self.engine.handle_raw_packet(raw)
        result["telemetry"] = parse_telemetry_payload(result["packet"].payload)
        result["note"] = note
        result["satellite"] = sat
        return result


class MastercontrolApp(tk.Tk):
    def __init__(self, rangepi_port: str | None = None, rangepi_baud: int = 115200, simulate: bool = True) -> None:
        super().__init__()

        self.title("CubeSat Control")
        self.geometry("1380x840")
        self.minsize(980, 680)
        self.configure(bg=BG)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self.api = FirmwareApi()
        self.running = True
        self.simulate = simulate
        self.tick = 0
        self.selected_index = 0
        self.log_lines = 0
        self.alerts = 0
        self.replay_rejects = 0
        self.env = EnvironmentState()
        self.map_sat_targets: list[tuple[float, float, Satellite]] = []
        self.hover_satellite: Satellite | None = None
        self.map_zoom = 11
        self.map_center_lat = 37.82
        self.map_center_lon = -122.28
        self.map_overview_zoom = 11
        self.map_detail_zoom = 15
        self.map_focused_satellite: Satellite | None = None
        self.map_redraw_after_id: str | None = None
        self.tile_cache_dir = Path(__file__).with_name("map_cache")
        self.tile_images: dict[tuple[int, int, int], tk.PhotoImage] = {}
        self.tile_failures: set[tuple[int, int, int]] = set()
        self.node_detail_window: tk.Toplevel | None = None
        self.node_detail_widgets: dict[str, tk.Widget] = {}
        self.command_sequence = 1
        self.command_queue: list[CommandRecord] = []
        self.alert_sequence = 1
        self.alert_records: list[AlertRecord] = []
        self.audit_sequence = 1
        self.audit_records: list[AuditRecord] = []
        self.packet_records: list[dict] = []
        self.alert_drawer_window: tk.Toplevel | None = None
        self.alert_drawer_widgets: dict[str, tk.Widget] = {}
        self.operations_window: tk.Toplevel | None = None
        self.operations_widgets: dict[str, tk.Widget] = {}
        self.operations_scope = "ALL"
        self.rendering_sat_list = False
        self.telemetry_cursor = 0
        self.pan_start: tuple[int, int, float, float] | None = None
        self.rangepi_port = rangepi_port
        self.rangepi_baud = rangepi_baud
        self.rangepi_queue: queue.Queue[tuple[str, object]] = queue.Queue()
        self.rangepi_running = False
        self.rangepi_thread: threading.Thread | None = None
        self.rangepi_link: SerialLink | None = None
        self.rangepi_lock = threading.Lock()
        self.rangepi_nodes_by_src: dict[int, str] = {}

        self.satellites = [
            Satellite("SF-MISSION", 0xA13C91E0, CYAN, 0.1, 0.0, "READY", "ML-KEM READY", 1240, 88, 9, 22.8, 360, 360, 37.7749, -122.4194, "mission-control"),
            Satellite("BERKELEY-LAB", 0x42D9BB10, GREEN, 1.8, 0.0, "HANDSHAKE", "KEM EXCHANGE", 771, 72, 7, 24.1, 420, 300, 37.8715, -122.2730, "research-node"),
            Satellite("OAKLAND-UPLINK", 0x93AF6721, AMBER, 3.0, 0.0, "MONITOR", "ML-DSA SIGNED", 2102, 64, 6, 25.6, 520, 260, 37.8044, -122.2712, "uplink-relay"),
            Satellite("SANJOSE-EDGE", 0x0FD21A90, MAGENTA, 4.2, 0.0, "STANDBY", "STANDBY", 380, 61, 8, 21.9, 600, 240, 37.3382, -121.8863, "edge-cache"),
            Satellite("MARIN-WATCH", 0x7E219A04, "#b8c2ff", 5.1, 0.0, "DOWNLINK", "AUTH TAG", 992, 79, 10, 23.5, 480, 280, 37.9735, -122.5311, "field-sensor"),
        ]
        self._seed_complex_demo_profiles()

        self._build_styles()
        for sat in self.satellites:
            self._init_node_history(sat)
        self._build_layout()
        self._write_terminal("CUBESAT CONTROL boot sequence complete", "info")
        self._write_terminal("Python dashboard online; firmware C API boundary attached", "info")
        self._write_alert("Simulated constellation pass queue initialized", "info")
        if self.rangepi_port:
            self._start_rangepi_reader()
        else:
            self._write_terminal("RangePi serial bridge idle; launch with --rangepi-port COMx when the USB radio is attached", "warn")
        self.after(100, self._loop)
        if self.simulate:
            self.after(900, self._generate_terminal_event)
        else:
            self._write_terminal("Simulation telemetry disabled; waiting for RangePi packets", "warn")

    def _build_styles(self) -> None:
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("Treeview", background=PANEL_DARK, foreground=TEXT, fieldbackground=PANEL_DARK, rowheight=28)
        style.configure("Treeview.Heading", background=PANEL, foreground=CYAN)
        style.map("Treeview", background=[("selected", "#12343a")], foreground=[("selected", TEXT)])
        style.configure("TNotebook", background=PANEL, borderwidth=0)
        style.configure("TNotebook.Tab", background=PANEL_DARK, foreground=MUTED, padding=(12, 7), font=("Consolas", 9))
        style.map("TNotebook.Tab", background=[("selected", "#12343a")], foreground=[("selected", CYAN)])

    def _seed_complex_demo_profiles(self) -> None:
        profiles = {
            "SF-MISSION": {
                "battery_percent": 96.0,
                "bus_voltage": 4.04,
                "current_ma": 132,
                "packet_loss_percent": 0.8,
                "replay_risk": 1.4,
                "radiation_cpm": 16,
                "pressure_hpa": 1014,
                "humidity_percent": 54,
                "firmware_version": "0.3.2-sec",
                "boot_count": 12,
            },
            "BERKELEY-LAB": {
                "battery_percent": 84.0,
                "bus_voltage": 3.86,
                "current_ma": 158,
                "packet_loss_percent": 2.1,
                "replay_risk": 4.8,
                "radiation_cpm": 21,
                "pressure_hpa": 1011,
                "humidity_percent": 48,
                "firmware_version": "0.3.1-lab",
                "boot_count": 31,
            },
            "OAKLAND-UPLINK": {
                "battery_percent": 71.0,
                "bus_voltage": 3.72,
                "current_ma": 196,
                "packet_loss_percent": 5.6,
                "replay_risk": 8.5,
                "radiation_cpm": 19,
                "pressure_hpa": 1010,
                "humidity_percent": 58,
                "firmware_version": "0.2.9-relay",
                "boot_count": 44,
                "last_fault": "lora_crc_burst",
            },
            "SANJOSE-EDGE": {
                "battery_percent": 63.0,
                "bus_voltage": 3.64,
                "current_ma": 118,
                "packet_loss_percent": 3.2,
                "replay_risk": 2.0,
                "radiation_cpm": 15,
                "pressure_hpa": 1009,
                "humidity_percent": 39,
                "firmware_version": "0.2.7-edge",
                "boot_count": 9,
            },
            "MARIN-WATCH": {
                "battery_percent": 89.0,
                "bus_voltage": 3.94,
                "current_ma": 141,
                "packet_loss_percent": 1.6,
                "replay_risk": 3.7,
                "radiation_cpm": 17,
                "pressure_hpa": 1015,
                "humidity_percent": 62,
                "firmware_version": "0.3.0-field",
                "boot_count": 18,
            },
        }
        for node in self.satellites:
            for key, value in profiles.get(node.name, {}).items():
                setattr(node, key, value)

    def _init_node_history(self, sat: Satellite) -> None:
        sat.history = {
            "temp": deque(maxlen=96),
            "link": deque(maxlen=96),
            "packets": deque(maxlen=96),
            "gas": deque(maxlen=96),
            "mag": deque(maxlen=96),
            "battery": deque(maxlen=96),
            "loss": deque(maxlen=96),
            "radiation": deque(maxlen=96),
        }
        for index in range(32):
            sat.history["temp"].append(sat.temperature_c + math.sin(index * 0.2 + sat.phase) * 0.4)
            sat.history["link"].append(sat.link_margin + math.sin(index * 0.17 + sat.phase) * 3.0)
            sat.history["packets"].append(sat.counter - (32 - index))
            sat.history["gas"].append(410 + ((sat.counter + index) % 80))
            sat.history["mag"].append(42 + math.sin(index * 0.15 + sat.phase) * 8.0)
            sat.history["battery"].append(sat.battery_percent - index * 0.01)
            sat.history["loss"].append(sat.packet_loss_percent + abs(math.sin(index * 0.21 + sat.phase)) * 1.4)
            sat.history["radiation"].append(sat.radiation_cpm + math.sin(index * 0.19 + sat.phase) * 3.0)

    def _build_layout(self) -> None:
        self.columnconfigure(0, weight=1)
        self.rowconfigure(1, weight=1)

        top = tk.Frame(self, bg=PANEL_DARK, highlightbackground=CYAN, highlightthickness=1)
        top.grid(row=0, column=0, sticky="ew", padx=12, pady=(12, 8))
        top.columnconfigure(0, weight=1)

        title = tk.Label(top, text="CUBESAT CONTROL", bg=PANEL_DARK, fg=TEXT, font=("Segoe UI", 24, "bold"))
        title.grid(row=0, column=0, sticky="w", padx=14, pady=(10, 0))

        subtitle = tk.Label(
            top,
            text="CubeSat node operations / firmware protocol API / Bay Area fixed-site map",
            bg=PANEL_DARK,
            fg=MUTED,
            font=("Consolas", 10),
        )
        subtitle.grid(row=1, column=0, sticky="w", padx=15, pady=(0, 12))

        controls = tk.Frame(top, bg=PANEL_DARK)
        controls.grid(row=0, column=1, rowspan=2, sticky="e", padx=12)

        self.link_label = tk.Label(controls, text="SIM-LINK ONLINE", bg="#030806", fg=GREEN, font=("Consolas", 10), padx=12, pady=8)
        self.link_label.pack(side="left", padx=4)
        self.add_node_button = self._button(controls, "ADD NODE", self._open_add_node_dialog)
        self.ops_button = self._button(controls, "OPERATIONS", self._open_operations_window)
        self.alerts_button = self._button(controls, "ALERTS", self._open_alert_drawer)
        self.street_button = self._button(controls, "STREET", self._open_street_view)
        self.pause_button = self._button(controls, "PAUSE", self._toggle_pause)
        self.replay_button = self._button(controls, "INJECT REPLAY", self._inject_replay)
        self.handoff_button = self._button(controls, "FORCE HANDOFF", self._force_handoff)

        main = tk.Frame(self, bg=BG)
        main.grid(row=1, column=0, sticky="nsew", padx=12, pady=(0, 12))
        main.columnconfigure(0, weight=2, minsize=300)
        main.columnconfigure(1, weight=4, minsize=430)
        main.columnconfigure(2, weight=2, minsize=280)
        main.rowconfigure(0, weight=3)
        main.rowconfigure(1, weight=2)

        self.sat_frame = self._panel(main, "SATELLITE WINDOWS")
        self.sat_frame.grid(row=0, column=0, sticky="nsew", padx=(0, 8), pady=(0, 8))

        self.map_frame = self._panel(main, "ORBITAL MAP")
        self.map_frame.grid(row=0, column=1, rowspan=2, sticky="nsew", padx=0, pady=0)

        self.state_frame = self._panel(main, "CONNECTION STATE")
        self.state_frame.grid(row=0, column=2, sticky="nsew", padx=(8, 0), pady=(0, 8))

        self.session_frame = self._panel(main, "NODE INSPECTOR")
        self.session_frame.grid(row=1, column=0, sticky="nsew", padx=(0, 8), pady=0)

        self.terminal_frame = self._panel(main, "MISSION SUMMARY")
        self.terminal_frame.grid(row=1, column=2, sticky="nsew", padx=(8, 0), pady=0)

        self._build_satellite_list()
        self._build_map()
        self._build_state()
        self._build_node_inspector()
        self._build_mission_summary()
        self._build_operations_model()
        self._build_charts()

    def _button(self, parent: tk.Widget, text: str, command) -> tk.Button:
        button = tk.Button(
            parent,
            text=text,
            command=command,
            bg=PANEL,
            fg=TEXT,
            activebackground="#11171a",
            activeforeground=TEXT,
            highlightbackground="#1c2428",
            highlightthickness=1,
            bd=0,
            padx=12,
            pady=8,
            font=("Segoe UI", 9),
        )
        button.pack(side="left", padx=4)
        return button

    def _panel(self, parent: tk.Widget, title: str) -> tk.Frame:
        outer = tk.Frame(parent, bg=PANEL, highlightbackground="#1c2428", highlightthickness=1)
        outer.rowconfigure(1, weight=1)
        outer.columnconfigure(0, weight=1)
        header = tk.Label(outer, text=title, bg=PANEL_DARK, fg=CYAN, font=("Segoe UI", 9, "bold"), anchor="w", padx=10, pady=8)
        header.grid(row=0, column=0, sticky="ew")
        body = tk.Frame(outer, bg=PANEL)
        body.grid(row=1, column=0, sticky="nsew", padx=8, pady=8)
        body.rowconfigure(0, weight=1)
        body.columnconfigure(0, weight=1)
        outer.body = body
        return outer

    def _build_satellite_list(self) -> None:
        self.sat_list = tk.Listbox(
            self.sat_frame.body,
            bg=PANEL_DARK,
            fg=TEXT,
            selectbackground="#12343a",
            selectforeground=TEXT,
            highlightthickness=0,
            bd=0,
            font=("Consolas", 11),
            activestyle="none",
        )
        self.sat_list.grid(row=0, column=0, sticky="nsew")
        self.sat_list.bind("<<ListboxSelect>>", self._select_satellite)

    def _build_map(self) -> None:
        self.map_canvas = tk.Canvas(self.map_frame.body, bg=MAP_DARK, highlightthickness=0)
        self.map_canvas.grid(row=0, column=0, sticky="nsew")
        self.map_canvas.bind("<Motion>", self._on_map_motion)
        self.map_canvas.bind("<Leave>", self._on_map_leave)
        self.map_canvas.bind("<Button-1>", self._on_map_click)
        self.map_canvas.bind("<ButtonPress-3>", self._on_map_pan_start)
        self.map_canvas.bind("<B3-Motion>", self._on_map_pan_drag)
        self.map_canvas.bind("<ButtonRelease-3>", self._on_map_pan_end)
        self.map_canvas.bind("<Double-Button-1>", self._on_map_double_click)
        self.map_canvas.bind("<MouseWheel>", self._on_map_wheel)
        self.readout = tk.Label(
            self.map_frame.body,
            bg=MAP_DARK,
            fg=TEXT,
            font=("Consolas", 11),
            anchor="w",
            padx=10,
            pady=8,
        )
        self.readout.grid(row=1, column=0, sticky="ew", pady=(8, 0))

    def _build_state(self) -> None:
        self.state_labels: dict[str, tk.Label] = {}
        labels = ["Accepted Frames", "Replay Rejects", "Average Link", "Ground Mode", "Lattice Layer", "LoRa Channel"]
        for index, label in enumerate(labels):
            tile = tk.Frame(self.state_frame.body, bg=PANEL_DARK, highlightbackground="#20282c", highlightthickness=1)
            tile.grid(row=index // 2, column=index % 2, sticky="nsew", padx=4, pady=4)
            self.state_frame.body.rowconfigure(index // 2, weight=1)
            self.state_frame.body.columnconfigure(index % 2, weight=1)
            tk.Label(tile, text=label.upper(), bg=PANEL_DARK, fg=MUTED, font=("Consolas", 9), anchor="w").pack(fill="x", padx=8, pady=(8, 0))
            value = tk.Label(tile, text="--", bg=PANEL_DARK, fg=TEXT, font=("Consolas", 18, "bold"), anchor="w")
            value.pack(fill="x", padx=8, pady=(4, 8))
            self.state_labels[label] = value

    def _build_node_inspector(self) -> None:
        self.inspector = tk.Text(
            self.session_frame.body,
            bg=PANEL_DARK,
            fg=TEXT,
            insertbackground=CYAN,
            highlightthickness=0,
            bd=0,
            font=("Consolas", 10),
            wrap="word",
            state="disabled",
            padx=10,
            pady=10,
        )
        self.inspector.tag_config("title", foreground=TEXT, font=("Consolas", 13, "bold"))
        self.inspector.tag_config("section", foreground=CYAN, font=("Consolas", 10, "bold"))
        self.inspector.tag_config("good", foreground=GREEN)
        self.inspector.tag_config("warn", foreground=AMBER)
        self.inspector.tag_config("bad", foreground=RED)
        self.inspector.tag_config("muted", foreground=MUTED)
        self.inspector.grid(row=0, column=0, sticky="nsew")

    def _build_mission_summary(self) -> None:
        self.summary_text = tk.Text(
            self.terminal_frame.body,
            bg="#010507",
            fg=TEXT,
            insertbackground=CYAN,
            highlightthickness=0,
            bd=0,
            font=("Consolas", 10),
            wrap="word",
            state="disabled",
        )
        self.summary_text.tag_config("info", foreground=CYAN)
        self.summary_text.tag_config("good", foreground=GREEN)
        self.summary_text.tag_config("warn", foreground=AMBER)
        self.summary_text.tag_config("bad", foreground=RED)
        self.summary_text.grid(row=0, column=0, sticky="nsew")

    def _build_operations_model(self) -> None:
        hidden = tk.Frame(self, bg=BG)
        self.ops_tabs = ttk.Notebook(hidden)
        self.ops_tabs.grid(row=0, column=0, sticky="nsew")

        terminal_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        command_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        schedule_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        intel_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        for tab in (terminal_tab, command_tab, schedule_tab, intel_tab):
            tab.rowconfigure(0, weight=1)
            tab.columnconfigure(0, weight=1)

        self.ops_tabs.add(terminal_tab, text="FEED")
        self.ops_tabs.add(command_tab, text="COMMAND")
        self.ops_tabs.add(schedule_tab, text="CONTACTS")
        self.ops_tabs.add(intel_tab, text="ENV")
        charts_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        alerts_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        nodes_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        queue_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        packets_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        audit_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        posture_tab = tk.Frame(self.ops_tabs, bg=PANEL)
        for tab in (charts_tab, alerts_tab, nodes_tab, queue_tab, packets_tab, audit_tab, posture_tab):
            tab.rowconfigure(0, weight=1)
            tab.columnconfigure(0, weight=1)
        self.ops_tabs.add(charts_tab, text="CHARTS")
        self.ops_tabs.add(alerts_tab, text="ALERTS")
        self.ops_tabs.add(nodes_tab, text="NODES")
        self.ops_tabs.add(queue_tab, text="QUEUE")
        self.ops_tabs.add(packets_tab, text="PACKETS")
        self.ops_tabs.add(audit_tab, text="AUDIT")
        self.ops_tabs.add(posture_tab, text="POSTURE")
        self.charts_tab = charts_tab
        self.alerts_tab = alerts_tab
        self.nodes_tab = nodes_tab
        self.queue_tab = queue_tab
        self.packets_tab = packets_tab
        self.audit_tab = audit_tab
        self.posture_tab = posture_tab

        self.terminal = tk.Text(
            terminal_tab,
            bg="#010507",
            fg=GREEN,
            insertbackground=CYAN,
            highlightthickness=0,
            bd=0,
            font=("Consolas", 10),
            wrap="word",
            state="disabled",
        )
        self.terminal.tag_config("info", foreground=CYAN)
        self.terminal.tag_config("warn", foreground=AMBER)
        self.terminal.tag_config("bad", foreground=RED)
        self.terminal.grid(row=0, column=0, sticky="nsew")

        self.command_output = tk.Text(
            command_tab,
            bg="#010507",
            fg=GREEN,
            insertbackground=CYAN,
            highlightthickness=0,
            bd=0,
            font=("Consolas", 10),
            wrap="word",
            height=10,
            state="disabled",
        )
        self.command_output.tag_config("info", foreground=CYAN)
        self.command_output.tag_config("warn", foreground=AMBER)
        self.command_output.tag_config("bad", foreground=RED)
        self.command_output.grid(row=0, column=0, sticky="nsew")

        command_bar = tk.Frame(command_tab, bg=PANEL_DARK)
        command_bar.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        command_bar.columnconfigure(1, weight=1)
        tk.Label(command_bar, text="cubesat>", bg=PANEL_DARK, fg=CYAN, font=("Consolas", 10)).grid(row=0, column=0, padx=(8, 6), pady=8)
        self.command_entry = tk.Entry(
            command_bar,
            bg="#02080b",
            fg=TEXT,
            insertbackground=CYAN,
            relief="flat",
            font=("Consolas", 10),
        )
        self.command_entry.grid(row=0, column=1, sticky="ew", pady=8)
        self.command_entry.bind("<Return>", self._run_command)
        run_button = tk.Button(
            command_bar,
            text="RUN",
            command=self._run_command,
            bg=PANEL,
            fg=TEXT,
            activebackground="#12343a",
            activeforeground=TEXT,
            highlightbackground=CYAN,
            highlightthickness=1,
            bd=0,
            padx=12,
            pady=6,
            font=("Consolas", 10),
        )
        run_button.grid(row=0, column=2, padx=8, pady=8)

        self.schedule_tree = ttk.Treeview(schedule_tab, columns=("sat", "state", "next", "reason"), show="headings")
        for column, width in [("sat", 110), ("state", 84), ("next", 88), ("reason", 160)]:
            self.schedule_tree.heading(column, text=column.upper())
            self.schedule_tree.column(column, width=width, anchor="w")
        self.schedule_tree.grid(row=0, column=0, sticky="nsew")

        self.env_text = tk.Text(
            intel_tab,
            bg="#010507",
            fg=TEXT,
            highlightthickness=0,
            bd=0,
            font=("Consolas", 10),
            wrap="word",
            state="disabled",
        )
        self.env_text.tag_config("good", foreground=GREEN)
        self.env_text.tag_config("warn", foreground=AMBER)
        self.env_text.tag_config("bad", foreground=RED)
        self.env_text.tag_config("info", foreground=CYAN)
        self.env_text.grid(row=0, column=0, sticky="nsew")

        self._write_command("type 'help' for commands", "info")

        self.alert_list = ttk.Treeview(alerts_tab, columns=("id", "sev", "status", "message"), show="headings")
        for column, width in [("id", 44), ("sev", 58), ("status", 72), ("message", 260)]:
            self.alert_list.heading(column, text=column.upper())
            self.alert_list.column(column, width=width, anchor="w")
        self.alert_list.grid(row=0, column=0, sticky="nsew")

        self.node_list = ttk.Treeview(nodes_tab, columns=("name", "role", "state", "session"), show="headings")
        for column, width in [("name", 116), ("role", 112), ("state", 82), ("session", 112)]:
            self.node_list.heading(column, text=column.upper())
            self.node_list.column(column, width=width, anchor="w")
        self.node_list.grid(row=0, column=0, sticky="nsew")

        self.queue_list = ttk.Treeview(queue_tab, columns=("id", "node", "cmd", "status", "detail"), show="headings")
        for column, width in [("id", 52), ("node", 116), ("cmd", 88), ("status", 74), ("detail", 170)]:
            self.queue_list.heading(column, text=column.upper())
            self.queue_list.column(column, width=width, anchor="w")
        self.queue_list.grid(row=0, column=0, sticky="nsew")

        self.packet_list = ttk.Treeview(packets_tab, columns=("tick", "node", "type", "counter", "session", "status"), show="headings")
        for column, width in [("tick", 56), ("node", 116), ("type", 78), ("counter", 78), ("session", 112), ("status", 94)]:
            self.packet_list.heading(column, text=column.upper())
            self.packet_list.column(column, width=width, anchor="w")
        self.packet_list.grid(row=0, column=0, sticky="nsew")

        self.audit_list = ttk.Treeview(audit_tab, columns=("id", "cat", "actor", "target", "action", "result"), show="headings")
        for column, width in [("id", 48), ("cat", 80), ("actor", 82), ("target", 114), ("action", 130), ("result", 92)]:
            self.audit_list.heading(column, text=column.upper())
            self.audit_list.column(column, width=width, anchor="w")
        self.audit_list.grid(row=0, column=0, sticky="nsew")

        self.posture_text = tk.Text(
            posture_tab,
            bg="#010507",
            fg=TEXT,
            highlightthickness=0,
            bd=0,
            font=("Consolas", 10),
            wrap="word",
            state="disabled",
        )
        self.posture_text.tag_config("good", foreground=GREEN)
        self.posture_text.tag_config("warn", foreground=AMBER)
        self.posture_text.tag_config("bad", foreground=RED)
        self.posture_text.tag_config("info", foreground=CYAN)
        self.posture_text.grid(row=0, column=0, sticky="nsew")

    def _build_charts(self) -> None:
        self.chart_canvas = tk.Canvas(self.charts_tab, bg="#010507", highlightthickness=0)
        self.chart_canvas.grid(row=0, column=0, sticky="nsew")

    def _open_operations_window(self) -> None:
        if self.operations_window is not None and self.operations_window.winfo_exists():
            self.operations_window.lift()
            return

        window = tk.Toplevel(self)
        window.title("CubeSat Operations")
        window.geometry("1080x720")
        window.minsize(780, 520)
        window.configure(bg=BG)
        window.protocol("WM_DELETE_WINDOW", self._close_operations_window)
        window.columnconfigure(0, weight=1)
        window.rowconfigure(0, weight=1)
        self.operations_window = window

        top = tk.Frame(window, bg=PANEL_DARK)
        top.grid(row=0, column=0, sticky="ew", padx=12, pady=(12, 0))
        top.columnconfigure(0, weight=1)
        tk.Label(
            top,
            text="OPERATIONS WORKSPACE",
            bg=PANEL_DARK,
            fg=TEXT,
            font=("Segoe UI", 16, "bold"),
            anchor="w",
            padx=14,
            pady=10,
        ).grid(row=0, column=0, sticky="ew")

        self.operations_scope_var = tk.StringVar(value=self.operations_scope)
        scope_values = ["ALL"] + [node.name for node in self.satellites]
        scope = ttk.Combobox(top, textvariable=self.operations_scope_var, values=scope_values, state="readonly", width=22)
        scope.grid(row=0, column=1, sticky="e", padx=12)
        scope.bind("<<ComboboxSelected>>", self._on_operations_scope_changed)
        self.operations_widgets["scope"] = scope

        mirror = ttk.Notebook(window)
        mirror.grid(row=1, column=0, sticky="nsew", padx=12, pady=12)
        window.rowconfigure(1, weight=1)
        self.operations_widgets = {"scope": scope}

        for name in ["FEED", "COMMAND", "CONTACTS", "CHARTS", "ALERTS", "NODES", "QUEUE", "PACKETS", "AUDIT", "POSTURE"]:
            tab = tk.Frame(mirror, bg=PANEL)
            tab.rowconfigure(0, weight=1)
            tab.columnconfigure(0, weight=1)
            mirror.add(tab, text=name)
            self._build_operations_mirror_tab(name, tab)
        self._refresh_operations_window()

    def _close_operations_window(self) -> None:
        if self.operations_window is not None:
            self.operations_window.destroy()
        self.operations_window = None
        self.operations_widgets = {}

    def _on_operations_scope_changed(self, _event=None) -> None:
        value = getattr(self, "operations_scope_var", tk.StringVar(value="ALL")).get()
        self.operations_scope = value or "ALL"
        self._refresh_operations_window()

    def _operations_nodes(self) -> list[Satellite]:
        if self.operations_scope == "ALL":
            return list(self.satellites)
        node = self._find_node(self.operations_scope)
        return [node] if node is not None else []

    def _start_rangepi_reader(self) -> None:
        if self.rangepi_running or not self.rangepi_port:
            return

        self.rangepi_running = True
        self.link_label.config(text="RANGEPI CONNECTING", fg=AMBER)
        self.rangepi_thread = threading.Thread(target=self._rangepi_reader_loop, name="rangepi-reader", daemon=True)
        self.rangepi_thread.start()

    def _rangepi_reader_loop(self) -> None:
        link = SerialLink(port=self.rangepi_port or "", baudrate=self.rangepi_baud, timeout=0.2)
        try:
            link.open()
            with self.rangepi_lock:
                self.rangepi_link = link
            self.rangepi_queue.put(("status", f"RangePi serial open {self.rangepi_port}@{self.rangepi_baud}"))

            while self.rangepi_running:
                line = link.read_line()
                if line:
                    self.rangepi_queue.put(("line", line))
        except Exception as exc:
            self.rangepi_queue.put(("error", f"RangePi serial error: {exc}"))
        finally:
            with self.rangepi_lock:
                self.rangepi_link = None
            try:
                link.close()
            except Exception:
                pass
            self.rangepi_queue.put(("closed", "RangePi serial bridge closed"))

    def _drain_rangepi_queue(self) -> None:
        while True:
            try:
                kind, payload = self.rangepi_queue.get_nowait()
            except queue.Empty:
                return

            if kind == "line" and isinstance(payload, bytes):
                self._handle_rangepi_line(payload)
            elif kind == "status":
                self.link_label.config(text="RANGEPI ONLINE", fg=GREEN)
                self._write_terminal(str(payload), "info")
            elif kind == "error":
                self.link_label.config(text="RANGEPI ERROR", fg=RED)
                self._write_terminal(str(payload), "bad")
            elif kind == "closed" and self.rangepi_port:
                self.link_label.config(text="RANGEPI OFFLINE", fg=AMBER)
                self._write_terminal(str(payload), "warn")

    def _handle_rangepi_line(self, line: bytes) -> None:
        try:
            raw_packet = parse_rangepi_line(line)
            result = self.api.engine.handle_raw_packet(raw_packet)
            packet = result["packet"]
        except Exception as exc:
            self._write_terminal(f"RANGEPI PARSE_ERROR {exc} raw={line[:80]!r}", "bad")
            return

        if packet.packet_type != HOPE_PACKET_TYPE_TELEMETRY:
            status = "accepted" if result["accepted"] else "rejected"
            self._write_terminal(
                f"RANGEPI packet type={packet.packet_type} {status} "
                f"src={packet.src_id} counter={packet.counter} session=0x{packet.session_id:08X}",
                "info" if result["accepted"] else "bad",
            )
            return

        try:
            telemetry = parse_telemetry_payload(packet.payload)
        except Exception as exc:
            self._write_terminal(f"RANGEPI telemetry decode failed: {exc}", "bad")
            return

        sat = self._hardware_node_for_packet(packet, telemetry)
        sat.counter = max(sat.counter, packet.counter)
        sat.fixed_latitude = telemetry.latitude
        sat.fixed_longitude = telemetry.longitude
        sat.temperature_c = telemetry.temperature_c
        sat.satellites_seen = telemetry.satellites
        sat.session_id = packet.session_id
        sat.online = True
        sat.command_state = "RANGEPI"
        sat.crypto_state = "HARDWARE DOWNLINK"
        sat.last_contact_tick = self.tick

        status = "accepted" if result["accepted"] else "rejected"
        if result["accepted"]:
            self._write_terminal(
                f"RANGEPI RX {sat.name} accepted counter={packet.counter} "
                f"lat={telemetry.latitude:.4f} lon={telemetry.longitude:.4f} "
                f"session=0x{packet.session_id:08X}",
                "info",
            )
        else:
            self.replay_rejects += 1
            self._write_alert(f"{sat.name} RangePi replay rejected counter={packet.counter}", "bad")

        self._record_packet(sat, "rangepi-rx", packet.counter, packet.session_id, status)

    def _hardware_node_for_packet(self, packet, telemetry) -> Satellite:
        for sat in self.satellites:
            if sat.session_id == packet.session_id:
                return sat

        mapped_name = self.rangepi_nodes_by_src.get(packet.src_id)
        if mapped_name:
            mapped = self._find_node(mapped_name)
            if mapped is not None:
                return mapped

        index = len(self.satellites)
        node = Satellite(
            f"ESP32-SRC{packet.src_id:02d}",
            packet.session_id,
            [CYAN, GREEN, AMBER, MAGENTA, "#b8c2ff"][index % 5],
            index * 0.7,
            0.0,
            "HARDWARE",
            "HARDWARE DOWNLINK",
            max(packet.counter, 1),
            76,
            telemetry.satellites,
            telemetry.temperature_c,
            420,
            300,
            telemetry.latitude,
            telemetry.longitude,
            "rangepi-downlink",
            command_state="RANGEPI",
        )
        self._init_node_history(node)
        self.satellites.append(node)
        self.rangepi_nodes_by_src[packet.src_id] = node.name
        self._write_alert(f"RangePi discovered hardware node {node.name} session=0x{packet.session_id:08X}", "info")
        return node

    def _send_rangepi_command(self, command: str) -> None:
        if not self.rangepi_port:
            self._write_command("RangePi bridge is not configured; restart with --rangepi-port COMx", "bad")
            return

        with self.rangepi_lock:
            link = self.rangepi_link

        if link is None:
            self._write_command("RangePi bridge is not connected", "bad")
            return

        try:
            link.write_line(command)
        except Exception as exc:
            self._write_command(f"RangePi command failed: {exc}", "bad")
            return

        self._record_audit("rangepi", "operator", "rangepi", command, "sent")
        self._write_command(f"rangepi> {command}", "info")

    def _on_close(self) -> None:
        self.rangepi_running = False
        with self.rangepi_lock:
            link = self.rangepi_link
        if link is not None:
            try:
                link.close()
            except Exception:
                pass
        self.destroy()

    def _scoped_log_text(self, text: str, label: str) -> str:
        if self.operations_scope == "ALL":
            return text
        scope = self.operations_scope.upper()
        lines = [line for line in text.splitlines() if scope in line.upper()]
        if lines:
            return "\n".join(lines) + "\n"
        return f"[{self.operations_scope}] no scoped {label.lower()} entries yet. Waiting for node traffic.\n"

    def _build_operations_mirror_tab(self, name: str, tab: tk.Frame) -> None:
        if name in {"FEED", "COMMAND", "POSTURE"}:
            text = tk.Text(tab, bg="#010507", fg=TEXT, highlightthickness=0, bd=0, font=("Consolas", 10), wrap="word", state="disabled")
            text.tag_config("info", foreground=CYAN)
            text.tag_config("warn", foreground=AMBER)
            text.tag_config("bad", foreground=RED)
            text.tag_config("good", foreground=GREEN)
            text.grid(row=0, column=0, sticky="nsew")
            self.operations_widgets[name] = text
            return

        columns_by_name = {
            "CONTACTS": ("sat", "state", "next", "reason"),
            "ALERTS": ("id", "severity", "status", "message"),
            "NODES": ("name", "role", "state", "session"),
            "QUEUE": ("id", "node", "cmd", "status", "detail"),
            "PACKETS": ("tick", "node", "type", "counter", "session", "status"),
            "AUDIT": ("id", "cat", "actor", "target", "action", "result"),
        }
        if name == "CHARTS":
            canvas = tk.Canvas(tab, bg="#010507", highlightthickness=0)
            canvas.grid(row=0, column=0, sticky="nsew")
            self.operations_widgets[name] = canvas
            return
        columns = columns_by_name[name]
        tree = ttk.Treeview(tab, columns=columns, show="headings")
        for column in columns:
            tree.heading(column, text=column.upper())
            tree.column(column, width=120, anchor="w")
        tree.grid(row=0, column=0, sticky="nsew")
        self.operations_widgets[name] = tree

    def _refresh_operations_window(self) -> None:
        if self.operations_window is None or not self.operations_window.winfo_exists():
            return
        feed = self.operations_widgets.get("FEED")
        if isinstance(feed, tk.Text):
            feed.configure(state="normal")
            feed.delete("1.0", tk.END)
            feed.insert(tk.END, self._scoped_log_text(self.terminal.get("1.0", tk.END), "feed"))
            feed.configure(state="disabled")
        posture = self.operations_widgets.get("POSTURE")
        if isinstance(posture, tk.Text):
            posture.configure(state="normal")
            posture.delete("1.0", tk.END)
            posture.insert(tk.END, self.posture_text.get("1.0", tk.END))
            posture.configure(state="disabled")
        command = self.operations_widgets.get("COMMAND")
        if isinstance(command, tk.Text):
            command.configure(state="normal")
            command.delete("1.0", tk.END)
            command.insert(tk.END, self._scoped_log_text(self.command_output.get("1.0", tk.END), "command"))
            command.configure(state="disabled")
        chart = self.operations_widgets.get("CHARTS")
        if isinstance(chart, tk.Canvas):
            self._render_mirror_chart(chart)

        self._refresh_operation_scope_values()
        self._refresh_operation_tables()

    def _refresh_operation_scope_values(self) -> None:
        scope = self.operations_widgets.get("scope")
        if isinstance(scope, ttk.Combobox):
            values = ["ALL"] + [node.name for node in self.satellites]
            scope.configure(values=values)
            if self.operations_scope not in values:
                self.operations_scope = "ALL"
                self.operations_scope_var.set("ALL")

    def _refresh_operation_tables(self) -> None:
        table_rows = {
            "CONTACTS": [
                (node.name, self._contact_state(node), f"{self._next_contact_seconds(node)}s", self._contact_reason(node))
                for node in sorted(self._operations_nodes(), key=self._next_contact_seconds)
            ],
            "ALERTS": [
                (record.alert_id, record.severity, record.status, record.message)
                for record in reversed(self.alert_records[-120:])
                if self.operations_scope == "ALL" or self.operations_scope in record.message
            ],
            "NODES": [
                (node.name, node.node_role, node.command_state, f"0x{node.session_id:08X}")
                for node in self._operations_nodes()
            ],
            "QUEUE": [
                (record.command_id, record.node_name, record.command, record.status, record.detail)
                for record in reversed(self.command_queue[-120:])
                if self.operations_scope == "ALL" or record.node_name == self.operations_scope
            ],
            "PACKETS": [
                (record["tick"], record["node"], record["type"], record["counter"], record["session"], record["status"])
                for record in reversed(self.packet_records[-160:])
                if self.operations_scope == "ALL" or record["node"] == self.operations_scope
            ],
            "AUDIT": [
                (record.audit_id, record.category, record.actor, record.target, record.action, record.result)
                for record in reversed(self.audit_records[-160:])
                if self.operations_scope == "ALL" or record.actor == self.operations_scope or record.target == self.operations_scope
            ],
        }
        for name, rows in table_rows.items():
            target = self.operations_widgets.get(name)
            if isinstance(target, ttk.Treeview):
                for item in target.get_children():
                    target.delete(item)
                for row in rows:
                    target.insert("", tk.END, values=row)

    def _write_command(self, message: str, tag: str = "") -> None:
        if not hasattr(self, "command_output"):
            return
        stamp = time.strftime("%H:%M:%S")
        self.command_output.configure(state="normal")
        self.command_output.insert(tk.END, f"[{stamp}] {message}\n", tag)
        self.command_output.see(tk.END)
        self.command_output.configure(state="disabled")

    def _run_command(self, _event=None) -> None:
        command = self.command_entry.get().strip()
        if not command:
            return
        self.command_entry.delete(0, tk.END)
        self._write_command(f"cubesat> {command}", "info")

        parts = command.lower().split()
        if parts[0] == "help":
            self._write_command("commands: status | node | addnode <name> <role> | delnode <name> | pair pin|kem|manual | rangepi <raw-cmd> | arm | isolate | connect | downlink | rotate | ping | zoomin | zoomout | google | street | track <1-5> | replay | handoff | pause | schedule | sessions", "info")
        elif parts[0] == "status":
            sat = self.satellites[self.selected_index]
            self._write_command(
                f"{sat.name}: role={sat.node_role} command={sat.command_state} contact={self._contact_state(sat)} "
                f"link={sat.link_margin:.0f}% session=0x{sat.session_id:08X}",
                "info",
            )
        elif parts[0] in ("node", "arm", "isolate", "connect", "downlink", "rotate", "ping"):
            self._run_node_command(parts[0])
        elif parts[0] == "addnode":
            self._add_node_from_command(parts)
        elif parts[0] == "delnode":
            self._delete_node_from_command(parts)
        elif parts[0] == "pair":
            self._pair_node_from_command(parts)
        elif parts[0] == "pairwizard":
            self._open_pairing_wizard()
        elif parts[0] in ("rangepi", "radio"):
            pieces = command.split(maxsplit=1)
            if len(pieces) < 2:
                self._write_command("usage: rangepi <raw command>", "bad")
            else:
                self._send_rangepi_command(pieces[1])
        elif parts[0] in ("zoomin", "zoom+"):
            self._zoom_in()
        elif parts[0] in ("zoomout", "zoom-"):
            self._zoom_out()
        elif parts[0] == "track" and len(parts) > 1 and parts[1].isdigit():
            index = max(1, min(len(self.satellites), int(parts[1]))) - 1
            self.selected_index = index
            self._write_command(f"tracking {self.satellites[index].name}", "info")
        elif parts[0] == "replay":
            self._inject_replay()
            self._write_command("manual replay injected into active session", "warn")
        elif parts[0] in ("google", "map", "maps"):
            self._open_google_maps()
            self._write_command("opened selected target in Google Maps", "info")
        elif parts[0] == "street":
            self._open_street_view()
            self._write_command("opened selected target in Street View", "info")
        elif parts[0] == "handoff":
            self._force_handoff()
            self._write_command("handoff complete", "info")
        elif parts[0] == "pause":
            self._toggle_pause()
            self._write_command("simulation toggled", "warn")
        elif parts[0] == "schedule":
            for sat in sorted(self.satellites, key=self._next_contact_seconds):
                self._write_command(f"{sat.name} {self._contact_state(sat)} next={self._next_contact_seconds(sat)}s reason={self._contact_reason(sat)}")
        elif parts[0] == "sessions":
            for sat in self.satellites:
                self._write_command(f"{sat.name} session=0x{sat.session_id:08X} crypto={sat.crypto_state} counter={sat.counter}")
        elif parts[0] == "env" and len(parts) > 1:
            self._apply_env_command(parts[1])
        else:
            self._write_command("unknown command; type help", "bad")

        self._render_all()

    def _apply_env_command(self, mode: str) -> None:
        if mode == "storm":
            self.env.radiation = 78
            self.env.ionosphere = 84
            self.env.ground_weather = 62
            self._write_command("environment set to storm; contact scheduler will hold weak passes", "warn")
            self._write_alert("space weather storm injected; adaptive contacts degraded", "warn")
        elif mode == "clear":
            self.env.radiation = 14
            self.env.ionosphere = 18
            self.env.ground_weather = 10
            self.env.eclipse = False
            self._write_command("environment cleared; scheduler back to nominal", "info")
        elif mode == "eclipse":
            self.env.eclipse = not self.env.eclipse
            self._write_command(f"eclipse mode {'enabled' if self.env.eclipse else 'disabled'}", "warn")
        else:
            self._write_command("env modes: storm | clear | eclipse", "bad")

    def _run_node_command(self, command: str) -> None:
        sat = self.satellites[self.selected_index]
        if command == "node":
            self._write_command(
                f"{sat.name} role={sat.node_role} online={sat.online} command={sat.command_state} "
                f"crypto={sat.crypto_state} session=0x{sat.session_id:08X}",
                "info",
            )
            return

        self._enqueue_command(sat, command, f"operator issued {command}")
        self._write_command(f"{command} queued for {sat.name}", "info")

    def _apply_node_command(self, sat: Satellite, command: str) -> None:
        if command == "arm":
            sat.command_state = "ARMED"
            sat.state = "READY"
            self._write_terminal(f"SECURE COMMAND arm issued to {sat.name}; ML-DSA authorization accepted", "info")
        elif command == "isolate":
            sat.command_state = "ISOLATED"
            sat.online = False
            sat.state = "QUARANTINE"
            self._write_alert(f"{sat.name} isolated from secure mesh", "warn")
        elif command == "connect":
            sat.command_state = "SECURE"
            sat.online = True
            sat.state = "READY"
            self._write_terminal(f"{sat.name} secure mesh connection restored", "info")
        elif command == "downlink":
            sat.command_state = "DOWNLINK"
            sat.state = "DOWNLINK"
            self._write_terminal(f"{sat.name} downlink window opened; telemetry route active", "info")
        elif command == "rotate":
            sat.crypto_state = "KEM ROTATED"
            sat.session_id = (sat.session_id + 0x10203) & 0xFFFFFFFF
            self._write_terminal(f"{sat.name} ML-KEM session rotated -> 0x{sat.session_id:08X}", "info")
        elif command == "ping":
            lat, lon = self._sat_position(sat)
            result = self.api.submit_telemetry(sat, lat, lon)
            self._record_packet(sat, "ping", result["packet"].counter, sat.session_id, "accepted" if result["accepted"] else "rejected")
            self._write_terminal(f"PING {sat.name} {'accepted' if result['accepted'] else 'rejected'} counter={result['packet'].counter}", "info" if result["accepted"] else "bad")

    def _add_node_from_command(self, parts: list[str]) -> None:
        if len(parts) < 3:
            self._write_command("usage: addnode <name> <role>", "bad")
            return
        name = parts[1].upper()
        role = parts[2]
        if any(node.name == name for node in self.satellites):
            self._write_command(f"node {name} already exists", "bad")
            return

        node = self._create_node(name, role)
        self._write_command(f"added {node.name} role={node.node_role}", "info")
        self._open_pairing_wizard()

    def _create_node(self, name: str, role: str, lat: float | None = None, lon: float | None = None) -> Satellite:
        index = len(self.satellites)
        node = Satellite(
            name.upper(),
            random.randint(0x10000000, 0xEFFFFFFF),
            [CYAN, GREEN, AMBER, MAGENTA, "#b8c2ff"][index % 5],
            index * 0.7,
            0.0,
            "PAIRING",
            "UNPAIRED",
            1,
            62,
            6,
            23.0,
            420,
            240,
            lat if lat is not None else 37.70 + (index * 0.045),
            lon if lon is not None else -122.48 + (index * 0.08),
            role,
            command_state="PAIRING",
        )
        self._init_node_history(node)
        self.satellites.append(node)
        self.selected_index = len(self.satellites) - 1
        self._write_alert(f"new node staged for pairing: {node.name}", "warn")
        self._render_all()
        return node

    def _delete_node_from_command(self, parts: list[str]) -> None:
        if len(parts) < 2:
            self._write_command("usage: delnode <name>", "bad")
            return
        name = parts[1].upper()
        if len(self.satellites) <= 1:
            self._write_command("cannot delete final node", "bad")
            return
        for index, node in enumerate(self.satellites):
            if node.name == name:
                del self.satellites[index]
                self.selected_index = min(self.selected_index, len(self.satellites) - 1)
                self._write_alert(f"node deleted: {name}", "warn")
                return
        self._write_command(f"node {name} not found", "bad")

    def _pair_node_from_command(self, parts: list[str]) -> None:
        method = parts[1] if len(parts) > 1 else "kem"
        sat = self.satellites[self.selected_index]
        if method not in {"pin", "kem", "manual"}:
            self._write_command("pair methods: pin | kem | manual", "bad")
            return
        sat.command_state = "SECURE"
        sat.online = True
        sat.crypto_state = "ML-KEM READY" if method == "kem" else f"PAIRED-{method.upper()}"
        sat.session_id = random.randint(0x10000000, 0xEFFFFFFF)
        self._write_terminal(f"PAIR {sat.name} method={method} session=0x{sat.session_id:08X}", "info")
        self._write_command(f"{sat.name} paired via {method}", "info")
        self._enqueue_command(sat, f"pair-{method}", "pairing procedure completed")

    def _open_add_node_dialog(self) -> None:
        dialog = tk.Toplevel(self)
        dialog.title("Add Secure Node")
        dialog.geometry("440x430")
        dialog.configure(bg=BG)
        dialog.columnconfigure(0, weight=1)

        tk.Label(dialog, text="ADD SECURE NODE", bg=PANEL_DARK, fg=TEXT, font=("Segoe UI", 16, "bold"), anchor="w", padx=14, pady=12).grid(row=0, column=0, sticky="ew", padx=12, pady=(12, 8))

        form = tk.Frame(dialog, bg=PANEL)
        form.grid(row=1, column=0, sticky="ew", padx=12, pady=8)
        form.columnconfigure(1, weight=1)

        fields = {}
        defaults = {
            "name": f"NODE-{len(self.satellites) + 1:02d}",
            "role": "sensor-node",
            "lat": f"{37.72 + len(self.satellites) * 0.035:.5f}",
            "lon": f"{-122.42 + len(self.satellites) * 0.045:.5f}",
        }
        for row, key in enumerate(["name", "role", "lat", "lon"]):
            tk.Label(form, text=key.upper(), bg=PANEL, fg=MUTED, font=("Consolas", 9)).grid(row=row, column=0, sticky="w", padx=12, pady=8)
            entry = tk.Entry(form, bg=PANEL_DARK, fg=TEXT, insertbackground=CYAN, relief="flat", font=("Consolas", 10))
            entry.insert(0, defaults[key])
            entry.grid(row=row, column=1, sticky="ew", padx=12, pady=8)
            fields[key] = entry

        method = tk.StringVar(value="kem")
        method_frame = tk.Frame(dialog, bg=PANEL)
        method_frame.grid(row=2, column=0, sticky="ew", padx=12, pady=8)
        tk.Label(method_frame, text="PAIRING METHOD", bg=PANEL, fg=MUTED, font=("Consolas", 9)).pack(anchor="w", padx=12, pady=(8, 3))
        for value, label in [("kem", "ML-KEM"), ("pin", "PIN"), ("manual", "Manual")]:
            tk.Radiobutton(method_frame, text=label, variable=method, value=value, bg=PANEL, fg=TEXT, selectcolor=PANEL_DARK, activebackground=PANEL, activeforeground=TEXT, font=("Segoe UI", 10)).pack(anchor="w", padx=12, pady=2)

        def create() -> None:
            try:
                lat = float(fields["lat"].get())
                lon = float(fields["lon"].get())
            except ValueError:
                self._write_command("lat/lon must be numbers", "bad")
                return
            node = self._create_node(fields["name"].get().strip() or defaults["name"], fields["role"].get().strip() or defaults["role"], lat, lon)
            self._pair_node_from_command(["pair", method.get()])
            self._open_node_detail_window()
            dialog.destroy()
            self._write_command(f"{node.name} added and paired via {method.get()}", "info")

        buttons = tk.Frame(dialog, bg=BG)
        buttons.grid(row=3, column=0, sticky="ew", padx=12, pady=(8, 12))
        tk.Button(buttons, text="CREATE NODE", command=create, bg=PANEL, fg=TEXT, bd=0, highlightbackground="#1c2428", highlightthickness=1, padx=12, pady=8).pack(side="left")
        tk.Button(buttons, text="CANCEL", command=dialog.destroy, bg=PANEL, fg=TEXT, bd=0, highlightbackground="#1c2428", highlightthickness=1, padx=12, pady=8).pack(side="right")

    def _enqueue_command(self, sat: Satellite, command: str, detail: str) -> CommandRecord:
        record = CommandRecord(
            command_id=self.command_sequence,
            node_name=sat.name,
            command=command,
            status="queued",
            created_tick=self.tick,
            updated_tick=self.tick,
            detail=detail,
        )
        self.command_sequence += 1
        self.command_queue.append(record)
        self._record_audit("command", "operator", sat.name, command, "queued")
        self.ops_tabs.tab(self.queue_tab, text=f"QUEUE ({len([cmd for cmd in self.command_queue if cmd.status in {'queued', 'sent'}])})")
        self._render_command_queue()
        return record

    def _process_command_queue(self) -> None:
        for record in self.command_queue:
            if record.status == "queued" and self.tick - record.created_tick >= 5:
                record.status = "sent"
                record.updated_tick = self.tick
                self._record_audit("command", "groundstation", record.node_name, record.command, "sent")
                self._write_terminal(f"CMD#{record.command_id} sent {record.node_name}:{record.command}", "info")
            elif record.status == "sent" and self.tick - record.updated_tick >= 10:
                node = self._find_node(record.node_name)
                if node is None:
                    record.status = "failed"
                    record.detail = "node missing"
                    record.updated_tick = self.tick
                    self._record_audit("command", "groundstation", record.node_name, record.command, "failed-node-missing")
                    self._write_alert(f"CMD#{record.command_id} failed; node missing", "bad")
                    continue
                if not node.online and record.command != "connect":
                    record.status = "failed"
                    record.detail = "node offline"
                    record.updated_tick = self.tick
                    self._record_audit("command", "groundstation", record.node_name, record.command, "failed-offline")
                    self._write_alert(f"CMD#{record.command_id} failed; {node.name} offline", "bad")
                    continue
                self._apply_node_command(node, record.command)
                record.status = "acked"
                record.detail = "acknowledged"
                record.updated_tick = self.tick
                self._record_audit("command", record.node_name, "groundstation", record.command, "acked")
                self._write_terminal(f"CMD#{record.command_id} acked {record.node_name}:{record.command}", "info")
        self._render_command_queue()

    def _find_node(self, name: str) -> Satellite | None:
        for node in self.satellites:
            if node.name == name:
                return node
        return None

    def _render_command_queue(self) -> None:
        if not hasattr(self, "queue_list"):
            return
        for item in self.queue_list.get_children():
            self.queue_list.delete(item)
        for record in reversed(self.command_queue[-80:]):
            self.queue_list.insert(
                "",
                tk.END,
                values=(record.command_id, record.node_name, record.command, record.status, record.detail),
            )
        pending = len([cmd for cmd in self.command_queue if cmd.status in {"queued", "sent"}])
        self.ops_tabs.tab(self.queue_tab, text=f"QUEUE ({pending})" if pending else "QUEUE")

    def _record_audit(self, category: str, actor: str, target: str, action: str, result: str) -> None:
        self.audit_records.append(
            AuditRecord(self.audit_sequence, category, actor, target, action, result, self.tick)
        )
        self.audit_sequence += 1
        self._render_audit_log()

    def _record_packet(self, sat: Satellite, packet_type: str, counter: int, session_id: int, status: str) -> None:
        self.packet_records.append(
            {
                "tick": self.tick,
                "node": sat.name,
                "type": packet_type,
                "counter": counter,
                "session": f"0x{session_id:08X}",
                "status": status,
            }
        )
        if len(self.packet_records) > 200:
            self.packet_records = self.packet_records[-200:]
        self._render_packet_log()

    def _render_packet_log(self) -> None:
        if not hasattr(self, "packet_list"):
            return
        for item in self.packet_list.get_children():
            self.packet_list.delete(item)
        for record in reversed(self.packet_records[-100:]):
            self.packet_list.insert(
                "",
                tk.END,
                values=(record["tick"], record["node"], record["type"], record["counter"], record["session"], record["status"]),
            )

    def _render_audit_log(self) -> None:
        if not hasattr(self, "audit_list"):
            return
        for item in self.audit_list.get_children():
            self.audit_list.delete(item)
        for record in reversed(self.audit_records[-120:]):
            self.audit_list.insert(
                "",
                tk.END,
                values=(record.audit_id, record.category, record.actor, record.target, record.action, record.result),
            )

    def _selected_target(self) -> tuple[Satellite, float, float]:
        sat = self.satellites[self.selected_index]
        lat, lon = self._sat_position(sat)
        return sat, lat, lon

    def _open_google_maps(self) -> None:
        sat, lat, lon = self._selected_target()
        url = f"https://www.google.com/maps/@?api=1&map_action=map&center={lat:.6f},{lon:.6f}&zoom=13"
        webbrowser.open(url)
        self._write_terminal(f"GOOGLE MAPS bridge opened for {sat.name} lat={lat:.5f} lon={lon:.5f}", "info")

    def _open_street_view(self) -> None:
        sat, lat, lon = self._selected_target()
        url = f"https://www.google.com/maps/@?api=1&map_action=pano&viewpoint={lat:.6f},{lon:.6f}"
        webbrowser.open(url)
        self._write_terminal(f"STREET VIEW bridge opened for {sat.name} lat={lat:.5f} lon={lon:.5f}", "info")

    def _open_pairing_wizard(self) -> None:
        sat = self.satellites[self.selected_index]
        wizard = tk.Toplevel(self)
        wizard.title("Pair Secure Node")
        wizard.geometry("460x420")
        wizard.configure(bg=BG)
        wizard.columnconfigure(0, weight=1)

        tk.Label(wizard, text=f"PAIR {sat.name}", bg=PANEL_DARK, fg=TEXT, font=("Segoe UI", 16, "bold"), anchor="w", padx=14, pady=12).grid(row=0, column=0, sticky="ew", padx=12, pady=(12, 8))

        method = tk.StringVar(value="kem")
        method_frame = tk.Frame(wizard, bg=PANEL)
        method_frame.grid(row=1, column=0, sticky="ew", padx=12, pady=8)
        for value, label in [("kem", "ML-KEM"), ("pin", "PIN"), ("manual", "Manual")]:
            tk.Radiobutton(method_frame, text=label, variable=method, value=value, bg=PANEL, fg=TEXT, selectcolor=PANEL_DARK, activebackground=PANEL, activeforeground=TEXT, font=("Segoe UI", 10)).pack(anchor="w", padx=12, pady=5)

        steps = tk.Listbox(wizard, bg=PANEL_DARK, fg=TEXT, highlightthickness=1, highlightbackground="#1c2428", bd=0, font=("Consolas", 10))
        steps.grid(row=2, column=0, sticky="nsew", padx=12, pady=8)
        wizard.rowconfigure(2, weight=1)

        def run_step(index: int = 0) -> None:
            flow = ["discovering node", "authenticating operator", "exchanging lattice key", "installing session", "paired"]
            if index >= len(flow):
                self._pair_node_from_command(["pair", method.get()])
                return
            steps.insert(tk.END, flow[index])
            self._enqueue_command(sat, f"pair-{method.get()}-{index + 1}", flow[index])
            wizard.after(450, lambda: run_step(index + 1))

        button_row = tk.Frame(wizard, bg=BG)
        button_row.grid(row=3, column=0, sticky="ew", padx=12, pady=(0, 12))
        tk.Button(button_row, text="START PAIRING", command=lambda: run_step(0), bg=PANEL, fg=TEXT, bd=0, highlightbackground="#1c2428", highlightthickness=1, padx=12, pady=8).pack(side="left")
        tk.Button(button_row, text="CLOSE", command=wizard.destroy, bg=PANEL, fg=TEXT, bd=0, highlightbackground="#1c2428", highlightthickness=1, padx=12, pady=8).pack(side="right")

    def _open_alert_drawer(self) -> None:
        if self.alert_drawer_window is not None and self.alert_drawer_window.winfo_exists():
            self.alert_drawer_window.lift()
            self._refresh_alert_drawer()
            return
        window = tk.Toplevel(self)
        window.title("CubeSat Alerts")
        window.geometry("760x460")
        window.configure(bg=BG)
        window.protocol("WM_DELETE_WINDOW", self._close_alert_drawer)
        window.columnconfigure(0, weight=1)
        window.rowconfigure(1, weight=1)
        self.alert_drawer_window = window

        tk.Label(window, text="ALERT DRAWER", bg=PANEL_DARK, fg=TEXT, font=("Segoe UI", 16, "bold"), anchor="w", padx=14, pady=12).grid(row=0, column=0, sticky="ew", padx=12, pady=(12, 8))
        tree = ttk.Treeview(window, columns=("id", "severity", "status", "message"), show="headings")
        for column, width in [("id", 52), ("severity", 86), ("status", 86), ("message", 460)]:
            tree.heading(column, text=column.upper())
            tree.column(column, width=width, anchor="w")
        tree.grid(row=1, column=0, sticky="nsew", padx=12, pady=8)
        buttons = tk.Frame(window, bg=BG)
        buttons.grid(row=2, column=0, sticky="ew", padx=12, pady=(0, 12))
        tk.Button(buttons, text="ACK SELECTED", command=lambda: self._set_selected_alert_status("acknowledged"), bg=PANEL, fg=TEXT, bd=0, highlightbackground="#1c2428", highlightthickness=1, padx=12, pady=8).pack(side="left", padx=(0, 8))
        tk.Button(buttons, text="RESOLVE SELECTED", command=lambda: self._set_selected_alert_status("resolved"), bg=PANEL, fg=TEXT, bd=0, highlightbackground="#1c2428", highlightthickness=1, padx=12, pady=8).pack(side="left")
        tk.Button(buttons, text="CLOSE", command=self._close_alert_drawer, bg=PANEL, fg=TEXT, bd=0, highlightbackground="#1c2428", highlightthickness=1, padx=12, pady=8).pack(side="right")
        self.alert_drawer_widgets = {"tree": tree}
        self._refresh_alert_drawer()

    def _close_alert_drawer(self) -> None:
        if self.alert_drawer_window is not None:
            self.alert_drawer_window.destroy()
        self.alert_drawer_window = None
        self.alert_drawer_widgets = {}

    def _refresh_alert_drawer(self) -> None:
        tree = self.alert_drawer_widgets.get("tree")
        if not isinstance(tree, ttk.Treeview):
            return
        for item in tree.get_children():
            tree.delete(item)
        for record in reversed(self.alert_records[-120:]):
            tree.insert("", tk.END, iid=str(record.alert_id), values=(record.alert_id, record.severity, record.status, record.message))

    def _set_selected_alert_status(self, status: str) -> None:
        tree = self.alert_drawer_widgets.get("tree")
        if not isinstance(tree, ttk.Treeview):
            return
        selection = tree.selection()
        if not selection:
            return
        selected_id = int(selection[0])
        for record in self.alert_records:
            if record.alert_id == selected_id:
                record.status = status
                self._write_terminal(f"ALERT#{record.alert_id} {status}: {record.message}", "info")
                break
        self._render_alerts()

    def _sat_position(self, sat: Satellite) -> tuple[float, float]:
        return sat.fixed_latitude, sat.fixed_longitude

    def _map_xy(self, lat: float, lon: float, width: int, height: int) -> tuple[float, float]:
        center_x, center_y = self._latlon_to_world_px(self.map_center_lat, self.map_center_lon, self.map_zoom)
        point_x, point_y = self._latlon_to_world_px(lat, lon, self.map_zoom)
        return (point_x - center_x) + (width / 2), (point_y - center_y) + (height / 2)

    def _latlon_to_world_px(self, lat: float, lon: float, zoom: int) -> tuple[float, float]:
        sin_lat = math.sin(math.radians(max(min(lat, 85.05112878), -85.05112878)))
        scale = TILE_SIZE * (2 ** zoom)
        x = (lon + 180.0) / 360.0 * scale
        y = (0.5 - math.log((1 + sin_lat) / (1 - sin_lat)) / (4 * math.pi)) * scale
        return x, y

    def _world_px_to_latlon(self, x: float, y: float, zoom: int) -> tuple[float, float]:
        scale = TILE_SIZE * (2 ** zoom)
        lon = (x / scale) * 360.0 - 180.0
        n = math.pi - 2.0 * math.pi * y / scale
        lat = math.degrees(math.atan(math.sinh(n)))
        return lat, lon

    def _tile_path(self, z: int, x: int, y: int) -> Path:
        return self.tile_cache_dir / "osm" / str(z) / str(x) / f"{y}.png"

    def _styled_tile_path(self, z: int, x: int, y: int) -> Path:
        return self.tile_cache_dir / "mono" / str(z) / str(x) / f"{y}.png"

    def _make_mono_tile(self, source: Path, target: Path) -> None:
        image = tk.PhotoImage(file=str(source))
        width = image.width()
        height = image.height()
        rows = []

        for y in range(height):
            pixels = []
            for x in range(width):
                r, g, b = image.get(x, y)
                gray = int((r * 0.299) + (g * 0.587) + (b * 0.114))
                gray = max(12, min(210, int((gray - 28) * 0.72)))
                if gray > 150:
                    gray = min(230, gray + 18)
                value = f"#{gray:02x}{gray:02x}{gray:02x}"
                pixels.append(value)
            rows.append("{" + " ".join(pixels) + "}")

        target.parent.mkdir(parents=True, exist_ok=True)
        image.put(" ".join(rows), to=(0, 0))
        image.write(str(target), format="png")

    def _load_tile(self, z: int, x: int, y: int) -> tk.PhotoImage | None:
        max_tile = 2 ** z
        if y < 0 or y >= max_tile:
            return None
        x = x % max_tile
        key = (z, x, y)
        if key in self.tile_images:
            return self.tile_images[key]
        if key in self.tile_failures:
            return None

        source_path = self._tile_path(z, x, y)
        styled_path = self._styled_tile_path(z, x, y)
        if not source_path.exists():
            try:
                source_path.parent.mkdir(parents=True, exist_ok=True)
                url = OSM_TILE_URL.format(z=z, x=x, y=y)
                request = Request(url, headers={"User-Agent": "CubeSat-groundstation-demo/0.1"})
                with urlopen(request, timeout=4) as response:
                    source_path.write_bytes(response.read())
            except Exception:
                self.tile_failures.add(key)
                return None

        if not styled_path.exists():
            try:
                self._make_mono_tile(source_path, styled_path)
            except Exception:
                styled_path = source_path

        try:
            image = tk.PhotoImage(file=str(styled_path))
        except tk.TclError:
            self.tile_failures.add(key)
            return None

        self.tile_images[key] = image
        return image

    def _draw_tile_map(self, width: int, height: int) -> bool:
        center_x, center_y = self._latlon_to_world_px(self.map_center_lat, self.map_center_lon, self.map_zoom)
        left = center_x - (width / 2)
        top = center_y - (height / 2)
        first_tile_x = math.floor(left / TILE_SIZE)
        first_tile_y = math.floor(top / TILE_SIZE)
        last_tile_x = math.floor((left + width) / TILE_SIZE)
        last_tile_y = math.floor((top + height) / TILE_SIZE)
        drew_any = False

        for tile_x in range(first_tile_x, last_tile_x + 1):
            for tile_y in range(first_tile_y, last_tile_y + 1):
                image = self._load_tile(self.map_zoom, tile_x, tile_y)
                if image is None:
                    continue
                screen_x = (tile_x * TILE_SIZE) - left
                screen_y = (tile_y * TILE_SIZE) - top
                self.map_canvas.create_image(screen_x, screen_y, image=image, anchor="nw")
                drew_any = True

        if drew_any:
            self.map_canvas.create_rectangle(0, 0, width, height, outline="#151b1f", width=1)
        return drew_any

    def _draw_local_fallback_map(self, width: int, height: int) -> None:
        self.map_canvas.create_rectangle(0, 0, width, height, fill=MAP_DARK, outline="#151b1f")

        center_x, center_y = self._map_xy(37.82, -122.28, width, height)
        for index in range(-12, 13):
            x = center_x + index * 34
            self.map_canvas.create_line(x, 0, x + 180, height, fill="#11181c")
            y = center_y + index * 28
            self.map_canvas.create_line(0, y, width, y - 80, fill="#11181c")

        bay_points = [
            self._map_xy(37.48, -122.52, width, height),
            self._map_xy(37.68, -122.38, width, height),
            self._map_xy(37.86, -122.33, width, height),
            self._map_xy(38.05, -122.40, width, height),
            self._map_xy(38.08, -122.23, width, height),
            self._map_xy(37.83, -122.10, width, height),
            self._map_xy(37.58, -122.18, width, height),
        ]
        flat_bay = [coord for point in bay_points for coord in point]
        self.map_canvas.create_polygon(*flat_bay, fill="#080b0d", outline="#273138", width=2)

        for lat, lon, label in [
            (37.7749, -122.4194, "SAN FRANCISCO"),
            (37.8715, -122.2730, "BERKELEY"),
            (37.8044, -122.2712, "OAKLAND"),
            (37.3382, -121.8863, "SAN JOSE"),
            (37.9735, -122.5311, "MARIN"),
        ]:
            x, y = self._map_xy(lat, lon, width, height)
            self.map_canvas.create_text(x + 42, y - 16, fill="#606c70", text=label, font=("Consolas", 9))

        self.map_canvas.create_text(
            16,
            16,
            anchor="nw",
            fill="#6f7b80",
            text="offline/local fallback map - internet tiles not loaded yet",
            font=("Consolas", 9),
        )

    def _contact_phase(self, sat: Satellite) -> int:
        return (self.tick + int(sat.phase * 20)) % sat.contact_period

    def _in_contact_window(self, sat: Satellite) -> bool:
        return self._contact_phase(sat) < sat.contact_duration

    def _contact_reason(self, sat: Satellite) -> str:
        if not self._in_contact_window(sat):
            return "below horizon"
        if self.env.risk > 70:
            return "environment hold"
        if sat.link_margin < 42:
            return "low link margin"
        return "clear"

    def _contact_state(self, sat: Satellite) -> str:
        return "OPEN" if self._contact_reason(sat) == "clear" else "WAIT"

    def _next_contact_seconds(self, sat: Satellite) -> int:
        phase = self._contact_phase(sat)
        if phase < sat.contact_duration:
            return 0
        return sat.contact_period - phase

    def _draw_world_land(self, width: int, height: int) -> None:
        for polygon in WORLD_POLYGONS:
            points = []
            for lon, lat in polygon:
                points.extend(self._map_xy(lat, lon, width, height))
            self.map_canvas.create_polygon(
                *points,
                fill="#cbd2d4",
                outline="#f4f7f8",
                stipple="gray50",
                width=1,
            )

    def _draw_hover_card(self, canvas: tk.Canvas, x: float, y: float, sat: Satellite, width: int, height: int) -> None:
        lat, lon = self._sat_position(sat)
        card_w = 250
        card_h = 112
        left = min(max(x + 16, 8), width - card_w - 8)
        top = min(max(y - 18, 8), height - card_h - 8)
        canvas.create_rectangle(left, top, left + card_w, top + card_h, fill="#030405", outline="#dfe7e8", width=1)
        lines = [
            sat.name,
            f"session 0x{sat.session_id:08X}",
            f"role {sat.node_role}",
            f"lat {lat:.5f} lon {lon:.5f}",
            f"node {sat.command_state} / {sat.state}",
            f"link {sat.link_margin:.0f}%  crypto {sat.crypto_state}",
        ]
        for index, line in enumerate(lines):
            color = TEXT if index == 0 else MUTED
            if index == 3 and self._contact_state(sat) == "OPEN":
                color = GREEN
            canvas.create_text(left + 10, top + 13 + index * 20, anchor="w", fill=color, text=line, font=("Consolas", 10 if index else 11, "bold" if index == 0 else "normal"))

    def _draw_map(self) -> None:
        canvas = self.map_canvas
        width = max(canvas.winfo_width(), 1)
        height = max(canvas.winfo_height(), 1)
        canvas.delete("all")
        self.map_sat_targets = []

        canvas.create_rectangle(0, 0, width, height, fill=MAP_DARK, outline="")
        if not self._draw_tile_map(width, height):
            self._draw_local_fallback_map(width, height)

        gs_x, gs_y = self._map_xy(37.8715, -122.273, width, height)
        pulse = 22 + math.sin(self.tick * 0.08) * 6
        canvas.create_oval(gs_x - 7, gs_y - 7, gs_x + 7, gs_y + 7, fill=AMBER, outline=AMBER)
        canvas.create_oval(gs_x - pulse, gs_y - pulse, gs_x + pulse, gs_y + pulse, outline=AMBER)
        canvas.create_text(gs_x + 42, gs_y - 14, fill=AMBER, text="GROUND", font=("Consolas", 10))

        selected = self.satellites[self.selected_index]
        for sat in self.satellites:
            lat, lon = self._sat_position(sat)
            x, y = self._map_xy(lat, lon, width, height)
            canvas.create_line(gs_x, gs_y, x, y, fill="#242b2f", width=1, dash=(2, 5))

        for sat in self.satellites:
            lat, lon = self._sat_position(sat)
            x, y = self._map_xy(lat, lon, width, height)
            radius = 8 if sat is selected else 5
            hovered = sat is self.hover_satellite
            outline = TEXT if hovered else sat.color
            canvas.create_oval(x - radius, y - radius, x + radius, y + radius, outline=outline, width=2)
            canvas.create_oval(x - 2, y - 2, x + 2, y + 2, fill=outline, outline=outline)
            canvas.create_text(x + 36, y, fill=outline, text=sat.name[-2:], font=("Consolas", 10))
            self.map_sat_targets.append((x, y, sat))
            if self._in_contact_window(sat):
                canvas.create_oval(x - 15, y - 15, x + 15, y + 15, outline="#7f8a8e")
            if sat is selected:
                reason = self._contact_reason(sat)
                line_color = "#dfe7e8" if reason == "clear" else AMBER
                canvas.create_line(gs_x, gs_y, x, y, fill=line_color, width=2, dash=() if reason == "clear" else (5, 4))
            if hovered:
                self._draw_hover_card(canvas, x, y, sat, width, height)

        lat, lon = self._sat_position(selected)
        self.readout.config(
            text=(
                f"{selected.name}  lat={lat: .5f}  lon={lon: .5f}  "
                f"session=0x{selected.session_id:08X}  role={selected.node_role}  "
                f"state={selected.command_state} zoom={self.map_zoom}"
            )
        )

    def _render_satellites(self) -> None:
        self.rendering_sat_list = True
        self.sat_list.delete(0, tk.END)
        for sat in self.satellites:
            lat, lon = self._sat_position(sat)
            contact = self._contact_state(sat)
            text = (
                f"{sat.name:<14} {sat.command_state:<8} {sat.link_margin:>3.0f}% {contact}\n"
                f"  {sat.node_role:<15} {sat.crypto_state:<13}\n"
                f"  lat {lat:>8.3f} lon {lon:>9.3f}"
            )
            self.sat_list.insert(tk.END, text)
        self.sat_list.selection_clear(0, tk.END)
        self.sat_list.selection_set(self.selected_index)
        self.sat_list.activate(self.selected_index)
        self.rendering_sat_list = False

    def _render_node_inspector(self) -> None:
        sat = self.satellites[self.selected_index]
        lat, lon = self._sat_position(sat)
        packet_rate = 0.7 + ((sat.counter % 8) * 0.11)
        avg_rssi = -118 + (sat.link_margin * 0.42)
        avg_snr = (sat.link_margin - 46) / 5.0
        gas_co2 = 410 + ((sat.counter + self.selected_index * 13) % 90)
        gas_voc = 0.14 + ((sat.counter % 17) * 0.015)
        mag_x = math.sin(sat.phase + self.tick * 0.002) * 34.0
        mag_y = math.cos(sat.phase + self.tick * 0.002) * 29.0
        mag_z = 41.0 + math.sin(sat.phase * 0.7) * 6.0
        accel = 0.01 + abs(math.sin(self.tick * 0.003 + sat.phase)) * 0.04
        gyro = abs(math.cos(self.tick * 0.002 + sat.phase)) * 0.08
        gnss_state = "LOCKED" if sat.satellites_seen >= 7 else "DEGRADED"
        lora_state = "LINKED" if sat.link_margin >= 55 else "WEAK"

        rows = [
            (sat.name + "\n", "title"),
            (f"{sat.node_role} / {sat.command_state}\n\n", "muted"),
            ("LOCATION\n", "section"),
            (f"lat/lon        {lat:.6f}, {lon:.6f}\n", ""),
            (f"contact        {self._contact_state(sat)} ({self._contact_reason(sat)})\n\n", "good" if self._contact_state(sat) == "OPEN" else "warn"),
            ("THERMAL + MOTION\n", "section"),
            (f"temperature    {sat.temperature_c:.1f} C\n", "good" if sat.temperature_c < 30 else "warn"),
            (f"acceleration   {accel:.3f} g\n", ""),
            (f"gyro drift     {gyro:.3f} deg/s\n\n", ""),
            ("GNSS + LORA\n", "section"),
            (f"gnss           {gnss_state} / {sat.satellites_seen} sats\n", "good" if gnss_state == "LOCKED" else "warn"),
            (f"lora           {lora_state} / margin {sat.link_margin:.0f}%\n", "good" if lora_state == "LINKED" else "warn"),
            (f"avg rssi       {avg_rssi:.1f} dBm\n", ""),
            (f"avg snr        {avg_snr:.1f} dB\n\n", ""),
            ("PACKETS + CRYPTO\n", "section"),
            (f"sent packets   {sat.counter:,}\n", ""),
            (f"packet rate    {packet_rate:.2f} pkt/s\n", ""),
            (f"packet loss    {sat.packet_loss_percent:.1f}%\n", "good" if sat.packet_loss_percent < 3 else "warn"),
            (f"replay risk    {sat.replay_risk:.1f}%\n", "good" if sat.replay_risk < 5 else "warn"),
            (f"session        0x{sat.session_id:08X}\n", ""),
            (f"crypto         {sat.crypto_state}\n\n", "good" if "STANDBY" not in sat.crypto_state else "warn"),
            ("SENSORS\n", "section"),
            (f"gas co2        {gas_co2} ppm\n", "good" if gas_co2 < 470 else "warn"),
            (f"gas voc        {gas_voc:.3f} ppm\n", "good" if gas_voc < 0.34 else "warn"),
            (f"mag x/y/z      {mag_x:+.1f} / {mag_y:+.1f} / {mag_z:+.1f} uT\n", ""),
            (f"radiation      {sat.radiation_cpm:.1f} cpm\n", "good" if sat.radiation_cpm < 30 else "warn"),
            (f"pressure       {sat.pressure_hpa:.1f} hPa\n", ""),
            (f"humidity       {sat.humidity_percent:.1f}%\n\n", ""),
            ("POWER + FIRMWARE\n", "section"),
            (f"battery        {sat.battery_percent:.1f}%\n", "good" if sat.battery_percent > 70 else "warn"),
            (f"bus voltage    {sat.bus_voltage:.2f} V\n", "good" if sat.bus_voltage > 3.7 else "warn"),
            (f"current        {sat.current_ma:.0f} mA\n", ""),
            (f"firmware       {sat.firmware_version}\n", ""),
            (f"boot count     {sat.boot_count}\n", ""),
            (f"last fault     {sat.last_fault}\n", "good" if sat.last_fault == "none" else "warn"),
        ]

        self.inspector.configure(state="normal")
        self.inspector.delete("1.0", tk.END)
        for text, tag in rows:
            self.inspector.insert(tk.END, text, tag)
        self.inspector.configure(state="disabled")

        self._refresh_node_detail_window()

    def _node_stats_text(self, sat: Satellite) -> str:
        lat, lon = self._sat_position(sat)
        avg_rssi = -118 + (sat.link_margin * 0.42)
        avg_snr = (sat.link_margin - 46) / 5.0
        gas_co2 = 410 + ((sat.counter + self.selected_index * 13) % 90)
        gas_voc = 0.14 + ((sat.counter % 17) * 0.015)
        mag_x = math.sin(sat.phase + self.tick * 0.002) * 34.0
        mag_y = math.cos(sat.phase + self.tick * 0.002) * 29.0
        mag_z = 41.0 + math.sin(sat.phase * 0.7) * 6.0
        accel = 0.01 + abs(math.sin(self.tick * 0.003 + sat.phase)) * 0.04
        gyro = abs(math.cos(self.tick * 0.002 + sat.phase)) * 0.08
        return (
            f"role           {sat.node_role}\n"
            f"command        {sat.command_state}\n"
            f"online         {sat.online}\n"
            f"session        0x{sat.session_id:08X}\n"
            f"crypto         {sat.crypto_state}\n\n"
            f"lat/lon        {lat:.6f}, {lon:.6f}\n"
            f"contact        {self._contact_state(sat)} / {self._contact_reason(sat)}\n\n"
            f"temperature    {sat.temperature_c:.1f} C\n"
            f"acceleration   {accel:.3f} g\n"
            f"gyro drift     {gyro:.3f} deg/s\n\n"
            f"gnss           {'LOCKED' if sat.satellites_seen >= 7 else 'DEGRADED'} / {sat.satellites_seen} sats\n"
            f"lora           {'LINKED' if sat.link_margin >= 55 else 'WEAK'} / {sat.link_margin:.0f}%\n"
            f"avg rssi       {avg_rssi:.1f} dBm\n"
            f"avg snr        {avg_snr:.1f} dB\n\n"
            f"sent packets   {sat.counter:,}\n"
            f"packet loss    {sat.packet_loss_percent:.1f}%\n"
            f"replay risk    {sat.replay_risk:.1f}%\n"
            f"gas co2        {gas_co2} ppm\n"
            f"gas voc        {gas_voc:.3f} ppm\n"
            f"mag x/y/z      {mag_x:+.1f} / {mag_y:+.1f} / {mag_z:+.1f} uT\n"
            f"radiation      {sat.radiation_cpm:.1f} cpm\n"
            f"battery        {sat.battery_percent:.1f}%\n"
            f"bus voltage    {sat.bus_voltage:.2f} V\n"
            f"current        {sat.current_ma:.0f} mA\n"
            f"firmware       {sat.firmware_version}\n"
            f"boot count     {sat.boot_count}\n"
            f"last fault     {sat.last_fault}\n"
        )

    def _open_node_detail_window(self) -> None:
        if self.node_detail_window is not None and self.node_detail_window.winfo_exists():
            self.node_detail_window.lift()
            self._refresh_node_detail_window()
            return

        window = tk.Toplevel(self)
        window.title("CubeSat Node Detail")
        window.geometry("820x620")
        window.minsize(620, 460)
        window.configure(bg=BG)
        window.protocol("WM_DELETE_WINDOW", self._close_node_detail_window)
        window.columnconfigure(0, weight=1)
        window.rowconfigure(1, weight=1)
        self.node_detail_window = window

        title = tk.Label(window, text="", bg=PANEL_DARK, fg=TEXT, font=("Segoe UI", 18, "bold"), anchor="w", padx=16, pady=12)
        title.grid(row=0, column=0, sticky="ew", padx=12, pady=(12, 8))

        body = ttk.Notebook(window)
        body.grid(row=1, column=0, sticky="nsew", padx=12, pady=(0, 12))

        overview_tab = tk.Frame(body, bg=BG)
        telemetry_tab = tk.Frame(body, bg=BG)
        security_tab = tk.Frame(body, bg=BG)
        commands_tab = tk.Frame(body, bg=BG)
        history_tab = tk.Frame(body, bg=BG)
        for tab in (overview_tab, telemetry_tab, security_tab, commands_tab, history_tab):
            tab.columnconfigure(0, weight=1)
            tab.rowconfigure(0, weight=1)
        body.add(overview_tab, text="OVERVIEW")
        body.add(telemetry_tab, text="TELEMETRY")
        body.add(security_tab, text="SECURITY")
        body.add(commands_tab, text="COMMANDS")
        body.add(history_tab, text="HISTORY")

        stats = tk.Text(overview_tab, bg=PANEL_DARK, fg=TEXT, insertbackground=CYAN, highlightthickness=1, highlightbackground="#1c2428", bd=0, font=("Consolas", 10), padx=12, pady=12, state="disabled")
        stats.grid(row=0, column=0, sticky="nsew")

        chart = tk.Canvas(telemetry_tab, bg="#010507", highlightthickness=1, highlightbackground="#1c2428")
        chart.grid(row=0, column=0, sticky="nsew")

        security = tk.Text(security_tab, bg=PANEL_DARK, fg=TEXT, insertbackground=CYAN, highlightthickness=1, highlightbackground="#1c2428", bd=0, font=("Consolas", 10), padx=12, pady=12, state="disabled")
        security.grid(row=0, column=0, sticky="nsew")

        history = ttk.Treeview(history_tab, columns=("id", "cmd", "status", "detail"), show="headings")
        for column, width in [("id", 58), ("cmd", 110), ("status", 90), ("detail", 360)]:
            history.heading(column, text=column.upper())
            history.column(column, width=width, anchor="w")
        history.grid(row=0, column=0, sticky="nsew")

        buttons = tk.Frame(commands_tab, bg=BG)
        buttons.grid(row=0, column=0, sticky="n")
        for label, command in [
            ("PING", lambda: self._run_node_command("ping")),
            ("ARM", lambda: self._run_node_command("arm")),
            ("DOWNLINK", lambda: self._run_node_command("downlink")),
            ("ROTATE", lambda: self._run_node_command("rotate")),
            ("ISOLATE", lambda: self._run_node_command("isolate")),
            ("CONNECT", lambda: self._run_node_command("connect")),
        ]:
            tk.Button(buttons, text=label, command=command, bg=PANEL, fg=TEXT, activebackground="#11171a", activeforeground=TEXT, bd=0, highlightbackground="#1c2428", highlightthickness=1, font=("Segoe UI", 9), padx=10, pady=7).pack(side="left", padx=3)

        self.node_detail_widgets = {"title": title, "stats": stats, "chart": chart, "security": security, "history": history}
        self._refresh_node_detail_window()

    def _close_node_detail_window(self) -> None:
        if self.node_detail_window is not None:
            self.node_detail_window.destroy()
        self.node_detail_window = None
        self.node_detail_widgets = {}

    def _refresh_node_detail_window(self) -> None:
        if self.node_detail_window is None or not self.node_detail_window.winfo_exists():
            return
        sat = self.satellites[self.selected_index]
        title = self.node_detail_widgets.get("title")
        stats = self.node_detail_widgets.get("stats")
        chart = self.node_detail_widgets.get("chart")
        security = self.node_detail_widgets.get("security")
        history = self.node_detail_widgets.get("history")
        if isinstance(title, tk.Label):
            title.config(text=f"{sat.name} / {sat.node_role}")
        if isinstance(stats, tk.Text):
            stats.configure(state="normal")
            stats.delete("1.0", tk.END)
            stats.insert(tk.END, self._node_stats_text(sat))
            stats.configure(state="disabled")
        if isinstance(chart, tk.Canvas):
            self._draw_detail_chart(chart, sat)
        if isinstance(security, tk.Text):
            security.configure(state="normal")
            security.delete("1.0", tk.END)
            security.insert(
                tk.END,
                (
                    f"SECURITY STATE\n\n"
                    f"session        0x{sat.session_id:08X}\n"
                    f"crypto         {sat.crypto_state}\n"
                    f"command state  {sat.command_state}\n"
                    f"online         {sat.online}\n\n"
                    f"replay policy  strictly increasing counters\n"
                    f"last counter   {sat.counter}\n"
                    f"pairing        {'complete' if sat.command_state != 'PAIRING' else 'pending'}\n"
                    f"auth mode      ML-DSA simulated gate\n"
                    f"kem mode       ML-KEM adapter boundary\n"
                ),
            )
            security.configure(state="disabled")
        if isinstance(history, ttk.Treeview):
            for item in history.get_children():
                history.delete(item)
            for record in reversed([cmd for cmd in self.command_queue if cmd.node_name == sat.name][-80:]):
                history.insert("", tk.END, values=(record.command_id, record.command, record.status, record.detail))

    def _draw_detail_chart(self, canvas: tk.Canvas, sat: Satellite) -> None:
        width = max(canvas.winfo_width(), 1)
        height = max(canvas.winfo_height(), 1)
        canvas.delete("all")
        history = sat.history or {}
        self._draw_chart_on(canvas, 12, 28, max(120, width - 24), max(80, (height - 64) // 2), list(history.get("link", [])), GREEN, "link margin")
        self._draw_chart_on(canvas, 12, 48 + ((height - 64) // 2), max(120, width - 24), max(80, (height - 64) // 2), list(history.get("battery", [])), AMBER, "battery")

    def _draw_chart_on(self, canvas: tk.Canvas, x: int, y: int, width: int, height: int, values, color: str, label: str) -> None:
        canvas.create_rectangle(x, y, x + width, y + height, outline="#20282c", fill="#030506")
        canvas.create_text(x + 10, y + 9, anchor="nw", fill=MUTED, text=label.upper(), font=("Consolas", 9))
        if len(values) < 2:
            return
        low = min(values)
        high = max(values)
        span = max(high - low, 1.0)
        points = []
        for index, value in enumerate(values):
            px = x + 10 + (index / (len(values) - 1)) * (width - 20)
            py = y + height - 12 - ((value - low) / span) * (height - 32)
            points.extend((px, py))
        canvas.create_line(*points, fill=color, width=2)
        canvas.create_text(x + width - 10, y + 9, anchor="ne", fill=TEXT, text=f"{values[-1]:.1f}", font=("Consolas", 10, "bold"))

    def _render_state(self) -> None:
        accepted = sum(sat.counter for sat in self.satellites)
        avg_link = sum(sat.link_margin for sat in self.satellites) / len(self.satellites)
        open_contacts = sum(1 for sat in self.satellites if self._contact_state(sat) == "OPEN")
        self.state_labels["Accepted Frames"].config(text=f"{accepted:,}")
        self.state_labels["Replay Rejects"].config(text=str(self.replay_rejects))
        self.state_labels["Average Link"].config(text=f"{avg_link:.0f}%")
        self.state_labels["Ground Mode"].config(text="LIVE" if self.running else "HOLD")
        self.state_labels["Lattice Layer"].config(text=f"RISK {self.env.risk:.0f}%")
        self.state_labels["LoRa Channel"].config(text=f"{open_contacts} OPEN")
        self._refresh_mission_summary()

    def _refresh_mission_summary(self) -> None:
        if not hasattr(self, "summary_text"):
            return
        selected = self.satellites[self.selected_index]
        open_alerts = len([alert for alert in self.alert_records if alert.status == "open"])
        pending_commands = len([cmd for cmd in self.command_queue if cmd.status in {"queued", "sent"}])
        recent_packets = len(self.packet_records[-20:])
        lines = [
            ("MISSION SUMMARY\n\n", "info"),
            (f"selected node     {selected.name}\n", ""),
            (f"health score      {self._node_health_score(selected)}%\n", "good" if self._node_health_score(selected) >= 80 else "warn"),
            (f"session           0x{selected.session_id:08X}\n", ""),
            (f"crypto            {selected.crypto_state}\n", "good" if "STANDBY" not in selected.crypto_state else "warn"),
            (f"link margin       {selected.link_margin:.0f}%\n", "good" if selected.link_margin >= 70 else "warn"),
            (f"battery           {selected.battery_percent:.1f}%\n\n", "good" if selected.battery_percent >= 70 else "warn"),
            ("OPS\n", "info"),
            (f"pending commands  {pending_commands}\n", "warn" if pending_commands else "good"),
            (f"open alerts       {open_alerts}\n", "bad" if open_alerts else "good"),
            (f"recent packets    {recent_packets}\n", ""),
            (f"replay rejects    {self.replay_rejects}\n\n", "warn" if self.replay_rejects else "good"),
            ("Open OPERATIONS for packet, audit, chart, queue, and posture workspaces.\n", ""),
        ]
        self.summary_text.configure(state="normal")
        self.summary_text.delete("1.0", tk.END)
        for text, tag in lines:
            self.summary_text.insert(tk.END, text, tag)
        self.summary_text.configure(state="disabled")

    def _render_schedule(self) -> None:
        for item in self.schedule_tree.get_children():
            self.schedule_tree.delete(item)
        for sat in sorted(self.satellites, key=self._next_contact_seconds):
            self.schedule_tree.insert(
                "",
                tk.END,
                values=(
                    sat.name[-6:],
                    self._contact_state(sat),
                    f"{self._next_contact_seconds(sat)}s",
                    self._contact_reason(sat),
                ),
            )

    def _render_environment(self) -> None:
        lines = [
            ("ENVIRONMENT MODEL\n", "info"),
            (f"radiation index      {self.env.radiation:5.1f}%\n", "warn" if self.env.radiation > 65 else "good"),
            (f"ionosphere noise     {self.env.ionosphere:5.1f}%\n", "warn" if self.env.ionosphere > 65 else "good"),
            (f"ground weather       {self.env.ground_weather:5.1f}%\n", "warn" if self.env.ground_weather > 65 else "good"),
            (f"eclipse mode         {'YES' if self.env.eclipse else 'NO'}\n", "warn" if self.env.eclipse else "good"),
            (f"operator load        {self.env.operator_load:5.1f}%\n", "warn" if self.env.operator_load > 75 else "good"),
            (f"contact risk         {self.env.risk:5.1f}%\n\n", "bad" if self.env.risk > 70 else "warn" if self.env.risk > 45 else "good"),
            ("SMART ROUTER\n", "info"),
            ("open windows require: above horizon, link margin >=42%, risk <=70%\n", ""),
            ("commands: help, status, node, addnode, delnode, pair, pairwizard, arm, isolate, connect, downlink, rotate, ping, zoomin, zoomout, google, street, track <1-5>\n", ""),
        ]
        self.env_text.configure(state="normal")
        self.env_text.delete("1.0", tk.END)
        for text, tag in lines:
            self.env_text.insert(tk.END, text, tag)
        self.env_text.configure(state="disabled")

    def _node_health_score(self, sat: Satellite) -> int:
        score = 100
        score -= max(0, 55 - sat.link_margin) * 0.7
        score -= max(0, 7 - sat.satellites_seen) * 6
        score -= max(0, sat.temperature_c - 30) * 2
        score -= max(0, 70 - sat.battery_percent) * 0.55
        score -= max(0, sat.packet_loss_percent - 3) * 3.0
        score -= max(0, sat.replay_risk - 5) * 2.0
        score -= max(0, 3.7 - sat.bus_voltage) * 20.0
        score -= max(0, sat.radiation_cpm - 28) * 0.8
        if not sat.online:
            score -= 35
        if sat.command_state in {"ISOLATED", "PAIRING"}:
            score -= 18
        if "STANDBY" in sat.crypto_state or "UNPAIRED" in sat.crypto_state:
            score -= 12
        return max(0, min(100, int(score)))

    def _render_security_posture(self) -> None:
        if not hasattr(self, "posture_text"):
            return
        online = len([node for node in self.satellites if node.online])
        paired = len([node for node in self.satellites if "UNPAIRED" not in node.crypto_state])
        pending_commands = len([cmd for cmd in self.command_queue if cmd.status in {"queued", "sent"}])
        open_alerts = len([alert for alert in self.alert_records if alert.status == "open"])
        rejected_packets = len([packet for packet in self.packet_records if packet["status"] != "accepted"])
        avg_health = sum(self._node_health_score(node) for node in self.satellites) / max(1, len(self.satellites))

        rows = [
            ("SECURITY POSTURE\n\n", "info"),
            (f"mesh online        {online}/{len(self.satellites)} nodes\n", "good" if online == len(self.satellites) else "warn"),
            (f"paired nodes       {paired}/{len(self.satellites)} nodes\n", "good" if paired == len(self.satellites) else "warn"),
            (f"avg health         {avg_health:5.1f}%\n", "good" if avg_health >= 80 else "warn" if avg_health >= 55 else "bad"),
            (f"pending commands   {pending_commands}\n", "warn" if pending_commands else "good"),
            (f"open alerts        {open_alerts}\n", "bad" if open_alerts else "good"),
            (f"rejected packets   {rejected_packets}\n\n", "warn" if rejected_packets else "good"),
            ("POLICY\n", "info"),
            ("command auth       simulated ML-DSA gate\n", ""),
            ("session exchange   ML-KEM adapter boundary\n", ""),
            ("replay filter      strict per-session monotonic counters\n", ""),
            ("audit logging      command lifecycle + security events\n\n", ""),
            ("NODE HEALTH\n", "info"),
        ]
        for node in self.satellites:
            score = self._node_health_score(node)
            tag = "good" if score >= 80 else "warn" if score >= 55 else "bad"
            rows.append((f"{node.name:<15} {score:3d}%  {node.command_state:<9} {node.crypto_state}\n", tag))

        self.posture_text.configure(state="normal")
        self.posture_text.delete("1.0", tk.END)
        for text, tag in rows:
            self.posture_text.insert(tk.END, text, tag)
        self.posture_text.configure(state="disabled")

    def _draw_series(self, x: int, y: int, width: int, height: int, values, color: str, label: str) -> None:
        self.chart_canvas.create_rectangle(x, y, x + width, y + height, outline="#20282c", fill="#030506")
        self.chart_canvas.create_text(x + 10, y + 10, anchor="nw", fill=MUTED, text=label.upper(), font=("Consolas", 9))
        if len(values) < 2:
            return
        min_value = min(values)
        max_value = max(values)
        span = max(max_value - min_value, 1.0)
        points = []
        for index, value in enumerate(values):
            px = x + 10 + (index / (len(values) - 1)) * (width - 20)
            py = y + height - 14 - ((value - min_value) / span) * (height - 34)
            points.extend((px, py))
        self.chart_canvas.create_line(*points, fill=color, width=2)
        self.chart_canvas.create_text(x + width - 10, y + 10, anchor="ne", fill=TEXT, text=f"{values[-1]:.1f}", font=("Consolas", 10, "bold"))

    def _render_charts(self) -> None:
        if not hasattr(self, "chart_canvas"):
            return
        sat = self.satellites[self.selected_index]
        history = sat.history or {}
        width = max(self.chart_canvas.winfo_width(), 1)
        height = max(self.chart_canvas.winfo_height(), 1)
        self.chart_canvas.delete("all")
        self.chart_canvas.create_text(12, 10, anchor="nw", fill=TEXT, text=f"{sat.name} LIVE METRICS", font=("Consolas", 11, "bold"))
        chart_w = max(160, (width - 36) // 2)
        chart_h = max(88, (height - 54) // 2)
        self._draw_series(12, 38, chart_w, chart_h, list(history.get("temp", [])), CYAN, "temperature c")
        self._draw_series(24 + chart_w, 38, chart_w, chart_h, list(history.get("link", [])), GREEN, "link margin")
        self._draw_series(12, 50 + chart_h, chart_w, chart_h, list(history.get("battery", [])), AMBER, "battery")
        self._draw_series(24 + chart_w, 50 + chart_h, chart_w, chart_h, list(history.get("loss", [])), MAGENTA, "packet loss")

    def _render_mirror_chart(self, canvas: tk.Canvas) -> None:
        width = max(canvas.winfo_width(), 1)
        height = max(canvas.winfo_height(), 1)
        canvas.delete("all")
        if self.operations_scope != "ALL":
            node = self._find_node(self.operations_scope) or self.satellites[self.selected_index]
            history = node.history or {}
            canvas.create_text(12, 10, anchor="nw", fill=TEXT, text=f"{node.name} OPERATIONS METRICS", font=("Consolas", 11, "bold"))
            chart_w = max(180, (width - 36) // 2)
            chart_h = max(110, (height - 54) // 2)
            self._draw_chart_on(canvas, 12, 38, chart_w, chart_h, list(history.get("temp", [])), CYAN, "temperature")
            self._draw_chart_on(canvas, 24 + chart_w, 38, chart_w, chart_h, list(history.get("link", [])), GREEN, "link margin")
            self._draw_chart_on(canvas, 12, 50 + chart_h, chart_w, chart_h, list(history.get("battery", [])), AMBER, "battery")
            self._draw_chart_on(canvas, 24 + chart_w, 50 + chart_h, chart_w, chart_h, list(history.get("loss", [])), MAGENTA, "packet loss")
            return

        canvas.create_text(12, 10, anchor="nw", fill=TEXT, text="FLEET HEALTH OVERVIEW", font=("Consolas", 11, "bold"))
        bar_top = 44
        bar_h = 24
        gap = 14
        for index, node in enumerate(self.satellites):
            y = bar_top + index * (bar_h + gap)
            score = self._node_health_score(node)
            canvas.create_text(14, y + 3, anchor="nw", fill=TEXT, text=node.name, font=("Consolas", 10))
            canvas.create_rectangle(180, y, max(181, width - 28), y + bar_h, outline="#20282c", fill="#030506")
            fill_w = (width - 210) * (score / 100)
            color = GREEN if score >= 80 else AMBER if score >= 55 else RED
            canvas.create_rectangle(180, y, 180 + fill_w, y + bar_h, outline="", fill=color)
            canvas.create_text(width - 18, y + 3, anchor="ne", fill=TEXT, text=f"{score}%", font=("Consolas", 10, "bold"))

    def _render_nodes_table(self) -> None:
        if not hasattr(self, "node_list"):
            return
        for item in self.node_list.get_children():
            self.node_list.delete(item)
        for sat in self.satellites:
            self.node_list.insert(
                "",
                tk.END,
                values=(sat.name, sat.node_role, sat.command_state, f"0x{sat.session_id:08X}"),
            )

    def _render_all(self) -> None:
        self._draw_map()
        self._render_satellites()
        self._render_node_inspector()
        self._render_state()
        self._render_schedule()
        self._render_environment()
        self._render_charts()
        self._render_nodes_table()
        self._render_command_queue()
        self._render_packet_log()
        self._render_audit_log()
        self._render_security_posture()
        self._refresh_operations_window()

    def _select_satellite(self, _event=None) -> None:
        if self.rendering_sat_list:
            return
        selection = self.sat_list.curselection()
        if selection:
            self.selected_index = selection[0]
            sat = self.satellites[self.selected_index]
            self._write_terminal(f"TARGET LOCK {sat.name} session=0x{sat.session_id:08X}", "info")
            self._open_node_detail_window()

    def _on_map_motion(self, event) -> None:
        nearest = None
        nearest_distance = 999.0
        for x, y, sat in self.map_sat_targets:
            distance = math.hypot(event.x - x, event.y - y)
            if distance < nearest_distance:
                nearest = sat
                nearest_distance = distance
        next_hover = nearest if nearest_distance <= 18 else None
        if next_hover is not self.hover_satellite:
            self.hover_satellite = next_hover
            self.map_canvas.configure(cursor="hand2" if next_hover else "")
            self._draw_map()

    def _on_map_leave(self, _event=None) -> None:
        if self.hover_satellite is not None:
            self.hover_satellite = None
            self.map_canvas.configure(cursor="")
            self._draw_map()

    def _on_map_click(self, _event=None) -> None:
        if self.hover_satellite is None:
            return
        self.selected_index = self.satellites.index(self.hover_satellite)
        self._write_terminal(f"MAP SELECT {self.hover_satellite.name} session=0x{self.hover_satellite.session_id:08X}", "info")
        self._open_node_detail_window()
        self._render_all()

    def _on_map_pan_start(self, event) -> None:
        self.pan_start = (event.x, event.y, self.map_center_lat, self.map_center_lon)
        self.map_canvas.configure(cursor="fleur")

    def _on_map_pan_drag(self, event) -> None:
        if self.pan_start is None:
            return
        start_x, start_y, start_lat, start_lon = self.pan_start
        center_x, center_y = self._latlon_to_world_px(start_lat, start_lon, self.map_zoom)
        new_center_x = center_x - (event.x - start_x)
        new_center_y = center_y - (event.y - start_y)
        self.map_center_lat, self.map_center_lon = self._world_px_to_latlon(new_center_x, new_center_y, self.map_zoom)
        self._schedule_map_redraw()

    def _on_map_pan_end(self, _event=None) -> None:
        self.pan_start = None
        self.map_canvas.configure(cursor="")

    def _on_map_double_click(self, _event=None) -> None:
        if self.hover_satellite is None:
            self._zoom_out(focus_selected=False, quiet=True)
            return

        if self.map_focused_satellite is self.hover_satellite and self.map_zoom >= self.map_detail_zoom:
            self.map_focused_satellite = None
            self.map_zoom = self.map_overview_zoom
            self.map_center_lat = 37.82
            self.map_center_lon = -122.28
            self._write_terminal("MAP overview restored", "info")
        else:
            self.selected_index = self.satellites.index(self.hover_satellite)
            self.map_focused_satellite = self.hover_satellite
            self.map_zoom = self.map_detail_zoom
            self.map_center_lat, self.map_center_lon = self._sat_position(self.hover_satellite)
            self._write_terminal(f"MAP detail focus {self.hover_satellite.name}", "info")

        self._render_all()

    def _on_map_wheel(self, event) -> None:
        if event.delta > 0:
            self._set_map_zoom(self.map_zoom + 1, quiet=True)
        else:
            self._set_map_zoom(self.map_zoom - 1, quiet=True)

    def _zoom_in(self, quiet: bool = False) -> None:
        self._set_map_zoom(self.map_zoom + 1, quiet=quiet)

    def _zoom_out(self, focus_selected: bool = True, quiet: bool = False) -> None:
        self._set_map_zoom(self.map_zoom - 1, focus_selected=focus_selected, quiet=quiet)

    def _set_map_zoom(self, zoom: int, focus_selected: bool = True, quiet: bool = False) -> None:
        previous_zoom = self.map_zoom
        self.map_zoom = max(8, min(18, zoom))
        if self.map_zoom == previous_zoom:
            return

        if self.map_zoom <= self.map_overview_zoom:
            self.map_focused_satellite = None
            self.map_center_lat = 37.82
            self.map_center_lon = -122.28
        elif focus_selected:
            sat = self.satellites[self.selected_index]
            self.map_center_lat, self.map_center_lon = self._sat_position(sat)
            if self.map_zoom >= self.map_detail_zoom:
                self.map_focused_satellite = sat

        if not quiet:
            self._write_terminal(f"MAP zoom set to {self.map_zoom}", "info")

        self._schedule_map_redraw()

    def _schedule_map_redraw(self) -> None:
        if self.map_redraw_after_id is not None:
            self.after_cancel(self.map_redraw_after_id)
        self.map_redraw_after_id = self.after(55, self._finish_map_redraw)

    def _finish_map_redraw(self) -> None:
        self.map_redraw_after_id = None
        self._render_all()

    def _write_terminal(self, message: str, tag: str = "") -> None:
        stamp = time.strftime("%H:%M:%S")
        self.terminal.configure(state="normal")
        self.terminal.insert(tk.END, f"[{stamp}] {message}\n", tag)
        self.terminal.see(tk.END)
        self.terminal.configure(state="disabled")
        self._refresh_mission_summary()
        self.log_lines += 1
        if self.log_lines > 300:
            self.terminal.configure(state="normal")
            self.terminal.delete("1.0", "40.0")
            self.terminal.configure(state="disabled")
            self.log_lines = 260

    def _write_alert(self, message: str, tag: str = "warn") -> None:
        self.alerts += 1
        severity = "critical" if tag == "bad" else "warn" if tag == "warn" else "info"
        self.alert_records.append(AlertRecord(self.alert_sequence, severity, message, self.tick))
        self.alert_sequence += 1
        self._record_audit("alert", "system", "operator", severity, "open")
        self._render_alerts()
        if tag == "bad":
            self.bell()
        self._write_terminal(f"ALERT {message}", tag)

    def _render_alerts(self) -> None:
        if not hasattr(self, "alert_list"):
            return
        for item in self.alert_list.get_children():
            self.alert_list.delete(item)
        for record in reversed(self.alert_records[-80:]):
            self.alert_list.insert("", tk.END, values=(record.alert_id, record.severity, record.status, record.message))
        open_count = len([alert for alert in self.alert_records if alert.status == "open"])
        self.ops_tabs.tab(self.alerts_tab, text=f"ALERTS ({open_count})" if open_count else "ALERTS")
        if self.alert_drawer_window is not None and self.alert_drawer_window.winfo_exists():
            self._refresh_alert_drawer()

    def _toggle_pause(self) -> None:
        self.running = not self.running
        self.pause_button.config(text="RESUME" if not self.running else "PAUSE")
        self.link_label.config(text="SIM-LINK HOLD" if not self.running else "SIM-LINK ONLINE", fg=AMBER if not self.running else GREEN)
        self._write_terminal("Simulation paused" if not self.running else "Simulation resumed", "warn" if not self.running else "info")

    def _inject_replay(self) -> None:
        sat = self.satellites[self.selected_index]
        lat, lon = self._sat_position(sat)
        result = self.api.submit_telemetry(sat, lat, lon, counter=max(1, sat.counter - 4), note="manual replay")
        status = "REJECTED" if not result["accepted"] else "ACCEPTED"
        if not result["accepted"]:
            self.replay_rejects += 1
        self._record_packet(sat, "replay-test", result["packet"].counter, sat.session_id, status.lower())
        self._write_alert(f"{sat.name} manual replay {status} counter={result['packet'].counter}", "bad" if not result["accepted"] else "warn")

    def _force_handoff(self) -> None:
        self.selected_index = (self.selected_index + 1) % len(self.satellites)
        sat = self.satellites[self.selected_index]
        self._write_terminal(f"HANDOFF complete; active target {sat.name}", "info")
        self._render_all()

    def _simulate_node_rx(self, sat: Satellite) -> None:
        lat, lon = self._sat_position(sat)
        result = self.api.submit_telemetry(sat, lat, lon)
        telemetry = result["telemetry"]

        if result["accepted"]:
            sat.counter += 1
            self._record_packet(sat, "telemetry", result["packet"].counter, sat.session_id, "accepted")
            self._write_terminal(
                f"RX {sat.name} accepted counter={result['packet'].counter} "
                f"lat={telemetry.latitude:.4f} lon={telemetry.longitude:.4f} "
                f"session=0x{sat.session_id:08X}",
                "info",
            )
        else:
            self.replay_rejects += 1
            self._record_packet(sat, "telemetry", result["packet"].counter, sat.session_id, "rejected")
            self._write_alert(f"{sat.name} replay rejected counter={result['packet'].counter}", "bad")

        if random.random() < 0.18:
            self._write_terminal(f"ML-KEM {sat.name} state={sat.crypto_state}; auth lane nominal; contact window adaptive")

    def _generate_terminal_event(self) -> None:
        open_sats = [sat for sat in self.satellites if self._contact_state(sat) == "OPEN"]
        if not open_sats:
            wait_sat = min(self.satellites, key=self._next_contact_seconds)
            self._write_terminal(
                f"CONTACT SCHEDULER hold: next={wait_sat.name} in {self._next_contact_seconds(wait_sat)}s "
                f"reason={self._contact_reason(wait_sat)} risk={self.env.risk:.0f}%",
                "warn",
            )
            self.after(900, self._generate_terminal_event)
            return

        online_open_sats = [sat for sat in open_sats if sat.online]
        if not online_open_sats:
            held_names = ", ".join(sat.name for sat in open_sats[:3])
            self._write_terminal(f"CONTACT SCHEDULER hold: open contacts offline ({held_names})", "warn")
            self.after(1200, self._generate_terminal_event)
            return

        batch_size = min(2, len(online_open_sats))
        for offset in range(batch_size):
            sat = online_open_sats[(self.telemetry_cursor + offset) % len(online_open_sats)]
            self._simulate_node_rx(sat)
        self.telemetry_cursor = (self.telemetry_cursor + batch_size) % len(online_open_sats)

        self.after(900, self._generate_terminal_event)

    def _loop(self) -> None:
        self._drain_rangepi_queue()
        if self.running:
            self.tick += 1
            self._process_command_queue()
            for index, sat in enumerate(self.satellites):
                sat.link_margin = max(24, min(98, sat.link_margin + math.sin(self.tick * 0.012 + index) * 0.05))
                sat.temperature_c += math.sin(self.tick * 0.006 + index) * 0.002
                sat.current_ma = max(85, min(240, sat.current_ma + math.sin(self.tick * 0.01 + index) * 0.18))
                sat.bus_voltage = max(3.45, min(4.15, sat.bus_voltage - (sat.current_ma / 900000.0) + math.sin(self.tick * 0.005 + index) * 0.0007))
                sat.battery_percent = max(12, min(100, sat.battery_percent - (sat.current_ma / 3500000.0)))
                sat.packet_loss_percent = max(0.1, min(18, sat.packet_loss_percent + math.sin(self.tick * 0.014 + index) * 0.015))
                sat.replay_risk = max(0.2, min(25, sat.replay_risk + math.sin(self.tick * 0.009 + index * 0.4) * 0.012))
                sat.radiation_cpm = max(6, min(80, sat.radiation_cpm + math.sin(self.tick * 0.011 + index) * 0.035))
                sat.pressure_hpa = max(990, min(1030, sat.pressure_hpa + math.sin(self.tick * 0.004 + index) * 0.01))
                sat.humidity_percent = max(15, min(88, sat.humidity_percent + math.sin(self.tick * 0.003 + index) * 0.015))
                if sat.history is not None:
                    sat.history["temp"].append(sat.temperature_c)
                    sat.history["link"].append(sat.link_margin)
                    sat.history["packets"].append(sat.counter)
                    sat.history["gas"].append(410 + ((sat.counter + index * 13) % 90))
                    sat.history["mag"].append(42 + math.sin(self.tick * 0.004 + sat.phase) * 8.0)
                    sat.history["battery"].append(sat.battery_percent)
                    sat.history["loss"].append(sat.packet_loss_percent)
                    sat.history["radiation"].append(sat.radiation_cpm)
                if sat.battery_percent < 25 and self.tick % 220 == 0:
                    sat.last_fault = "low_power_margin"
                    self._write_alert(f"{sat.name} low battery margin {sat.battery_percent:.1f}%", "warn")
                if sat.packet_loss_percent > 10 and self.tick % 260 == 0:
                    sat.last_fault = "packet_loss_elevated"
                    self._write_alert(f"{sat.name} elevated packet loss {sat.packet_loss_percent:.1f}%", "warn")
                if sat.replay_risk > 14 and self.tick % 310 == 0:
                    sat.last_fault = "replay_risk_elevated"
                    self._write_alert(f"{sat.name} replay risk elevated {sat.replay_risk:.1f}%", "bad")
                if self._contact_state(sat) == "OPEN":
                    sat.last_contact_tick = self.tick
                if sat.link_margin < 45:
                    sat.state = "LOW SNR"
                elif not sat.online:
                    sat.state = "OFFLINE"
                elif sat.command_state in ("ARMED", "DOWNLINK", "ISOLATED"):
                    sat.state = sat.command_state
                elif self._contact_state(sat) == "WAIT":
                    sat.state = "SCHEDULED"
                elif sat.crypto_state == "KEM EXCHANGE":
                    sat.state = "HANDSHAKE"
                elif index % 3 == 0:
                    sat.state = "DOWNLINK"
                else:
                    sat.state = "TRACK"

        self._render_all()
        self.after(90, self._loop)


def main() -> None:
    parser = argparse.ArgumentParser(description="CubeSat Control dashboard")
    parser.add_argument("--rangepi-port", help="RangePi serial port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="RangePi serial baud rate")
    parser.add_argument("--no-sim", action="store_true", help="Disable fake telemetry and wait for RangePi packets")
    args = parser.parse_args()

    app = MastercontrolApp(
        rangepi_port=args.rangepi_port,
        rangepi_baud=args.baud,
        simulate=not args.no_sim,
    )
    app.mainloop()


if __name__ == "__main__":
    main()
