import argparse
import binascii
import math
import queue
import random
import sys
import threading
import time
import tkinter as tk
from collections import deque
from pathlib import Path
from tkinter import ttk

if __package__ in {None, ""}:
    repo_root = Path(__file__).resolve().parents[2]
    if str(repo_root) not in sys.path:
        sys.path.insert(0, str(repo_root))

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - shown in UI at runtime
    serial = None
    list_ports = None

from groundstation.backend.event_engine import EventEngine
from groundstation.backend.packet_parser import encode_command_packet
from groundstation.backend.rangepi import format_packet_result, parse_rangepi_line
from groundstation.models.command import command_opcode_from_name


BG = "#020203"
PANEL = "#050607"
PANEL_DARK = "#000000"
CYAN = "#8bdde8"
GREEN = "#9effb1"
AMBER = "#e6c36a"
RED = "#ff6678"
TEXT = "#eef7f8"
MUTED = "#9aa3a6"
GRID = "#151b1f"


def _serial_ports() -> list[str]:
    if list_ports is None:
        return []
    return [port.device for port in list_ports.comports()]


def _printable(data: bytes) -> str:
    text = data.decode("utf-8", errors="replace").strip()
    if text and sum(ch.isprintable() for ch in text) >= max(1, len(text) * 0.75):
        return text
    return data.hex(" ").upper()


class RangePiViewer(tk.Tk):
    def __init__(self, port: str | None = None, baud: int = 115200) -> None:
        super().__init__()
        self.title("RangePi Link Viewer")
        self.geometry("1100x720")
        self.minsize(880, 560)
        self.configure(bg=BG)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self.serial = None
        self.reader_thread: threading.Thread | None = None
        self.reader_running = threading.Event()
        self.rx_queue: queue.Queue[tuple[str, object]] = queue.Queue()
        self.engine = EventEngine()

        self.bytes_rx = 0
        self.bytes_tx = 0
        self.lines_rx = 0
        self.packets_rx = 0
        self.parse_errors = 0
        self.tx_counter = 100
        self.last_rx_at = 0.0
        self.opened_at = 0.0
        self.activity = 0.0
        self.line_buffer = bytearray()
        self.rx_history = deque([0] * 96, maxlen=96)

        ports = _serial_ports()
        default_port = port or (ports[0] if ports else "COM5")
        self.port_var = tk.StringVar(value=default_port)
        self.baud_var = tk.StringVar(value=str(baud))
        self.session_var = tk.StringVar(value="0x12345678")
        self.dst_var = tk.StringVar(value="1")
        self.raw_var = tk.StringVar(value="help")

        self._build_styles()
        self._build_layout(ports)
        self.after(80, self._ui_loop)

        if port:
            self.after(120, self._connect)

    def _build_styles(self) -> None:
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("TCombobox", fieldbackground=PANEL_DARK, background=PANEL, foreground=TEXT)

    def _build_layout(self, ports: list[str]) -> None:
        self.columnconfigure(0, weight=1)
        self.rowconfigure(2, weight=1)

        top = tk.Frame(self, bg=PANEL_DARK, highlightbackground=CYAN, highlightthickness=1)
        top.grid(row=0, column=0, sticky="ew", padx=12, pady=(12, 8))
        top.columnconfigure(4, weight=1)

        tk.Label(top, text="RANGEPI LINK VIEWER", bg=PANEL_DARK, fg=TEXT, font=("Segoe UI", 22, "bold")).grid(row=0, column=0, columnspan=4, sticky="w", padx=14, pady=(10, 0))
        tk.Label(top, text="USB serial monitor / LoRa bridge probe / packet parser", bg=PANEL_DARK, fg=MUTED, font=("Consolas", 10)).grid(row=1, column=0, columnspan=4, sticky="w", padx=15, pady=(0, 10))

        form = tk.Frame(top, bg=PANEL_DARK)
        form.grid(row=0, column=4, rowspan=2, sticky="e", padx=12, pady=10)
        tk.Label(form, text="PORT", bg=PANEL_DARK, fg=MUTED, font=("Consolas", 8)).grid(row=0, column=0, sticky="w")
        self.port_combo = ttk.Combobox(form, textvariable=self.port_var, values=ports, width=12)
        self.port_combo.grid(row=1, column=0, padx=(0, 8))
        tk.Label(form, text="BAUD", bg=PANEL_DARK, fg=MUTED, font=("Consolas", 8)).grid(row=0, column=1, sticky="w")
        tk.Entry(form, textvariable=self.baud_var, bg="#02080b", fg=TEXT, insertbackground=CYAN, width=10).grid(row=1, column=1, padx=(0, 8))
        self.connect_button = self._button(form, "CONNECT", self._connect)
        self.connect_button.grid(row=1, column=2, sticky="ew", padx=4, pady=4)
        self.disconnect_button = self._button(form, "DISCONNECT", self._disconnect)
        self.disconnect_button.grid(row=1, column=3, sticky="ew", padx=4, pady=4)

        stats = tk.Frame(self, bg=BG)
        stats.grid(row=1, column=0, sticky="ew", padx=12, pady=(0, 8))
        for index in range(6):
            stats.columnconfigure(index, weight=1)
        self.stat_labels: dict[str, tk.Label] = {}
        for index, label in enumerate(["USB", "RX BYTES", "RX LINES", "PACKETS", "TX BYTES", "IDLE"]):
            tile = tk.Frame(stats, bg=PANEL_DARK, highlightbackground="#20282c", highlightthickness=1)
            tile.grid(row=0, column=index, sticky="nsew", padx=4)
            tk.Label(tile, text=label, bg=PANEL_DARK, fg=MUTED, font=("Consolas", 8)).pack(anchor="w", padx=8, pady=(8, 0))
            value = tk.Label(tile, text="--", bg=PANEL_DARK, fg=TEXT, font=("Consolas", 15, "bold"))
            value.pack(anchor="w", padx=8, pady=(3, 8))
            self.stat_labels[label] = value

        body = tk.Frame(self, bg=BG)
        body.grid(row=2, column=0, sticky="nsew", padx=12, pady=(0, 12))
        body.columnconfigure(0, weight=3)
        body.columnconfigure(1, weight=2)
        body.rowconfigure(0, weight=1)

        left = tk.Frame(body, bg=PANEL, highlightbackground="#20282c", highlightthickness=1)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
        left.rowconfigure(1, weight=1)
        left.columnconfigure(0, weight=1)
        tk.Label(left, text="RAW SERIAL FEED", bg=PANEL_DARK, fg=CYAN, font=("Consolas", 10, "bold"), anchor="w", padx=10, pady=8).grid(row=0, column=0, sticky="ew")
        self.log = tk.Text(left, bg="#000405", fg=TEXT, insertbackground=CYAN, highlightthickness=0, bd=0, font=("Consolas", 10), wrap="word")
        self.log.grid(row=1, column=0, sticky="nsew", padx=8, pady=(0, 8))
        for name, color in {"info": CYAN, "good": GREEN, "warn": AMBER, "bad": RED, "muted": MUTED}.items():
            self.log.tag_config(name, foreground=color)

        right = tk.Frame(body, bg=BG)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        right.rowconfigure(1, weight=1)

        self.static_canvas = tk.Canvas(right, bg=PANEL_DARK, highlightthickness=1, highlightbackground="#20282c", height=220)
        self.static_canvas.grid(row=0, column=0, sticky="ew")

        controls = tk.Frame(right, bg=PANEL, highlightbackground="#20282c", highlightthickness=1)
        controls.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        for index in range(5):
            controls.columnconfigure(index, weight=1)
        tk.Label(controls, text="BRIDGE PROBES", bg=PANEL_DARK, fg=CYAN, font=("Consolas", 10, "bold"), anchor="w", padx=10, pady=8).grid(row=0, column=0, columnspan=5, sticky="ew")

        for column, (label, command) in enumerate(
            [
                ("NEWLINE", lambda: self._send_raw("")),
                ("HELP", lambda: self._send_raw("help")),
                ("TX PING", lambda: self._send_command("ping")),
                ("TX SELFTEST", lambda: self._send_command("selftest")),
                ("TX ROTATE", lambda: self._send_command("rotate")),
            ]
        ):
            self._button(controls, label, command).grid(row=1, column=column, sticky="ew", padx=4, pady=4)

        tk.Label(controls, text="SESSION", bg=PANEL, fg=MUTED, font=("Consolas", 8)).grid(row=2, column=0, sticky="w", padx=10, pady=(14, 0))
        tk.Entry(controls, textvariable=self.session_var, bg="#02080b", fg=TEXT, insertbackground=CYAN).grid(row=3, column=0, sticky="ew", padx=10, pady=(2, 0))
        tk.Label(controls, text="DEST NODE", bg=PANEL, fg=MUTED, font=("Consolas", 8)).grid(row=2, column=1, sticky="w", padx=10, pady=(14, 0))
        tk.Entry(controls, textvariable=self.dst_var, bg="#02080b", fg=TEXT, insertbackground=CYAN).grid(row=3, column=1, sticky="ew", padx=10, pady=(2, 0))

        tk.Label(controls, text="RAW SERIAL LINE", bg=PANEL, fg=MUTED, font=("Consolas", 8)).grid(row=4, column=0, columnspan=2, sticky="w", padx=10, pady=(14, 0))
        tk.Entry(controls, textvariable=self.raw_var, bg="#02080b", fg=TEXT, insertbackground=CYAN).grid(row=5, column=0, columnspan=2, sticky="ew", padx=10, pady=(2, 0))
        self._button(controls, "SEND RAW", lambda: self._send_raw(self.raw_var.get())).grid(row=5, column=2, sticky="ew", padx=10)

        self.status_text = tk.Text(controls, bg="#000405", fg=TEXT, height=8, insertbackground=CYAN, highlightthickness=0, bd=0, font=("Consolas", 9), wrap="word")
        self.status_text.grid(row=6, column=0, columnspan=4, sticky="nsew", padx=10, pady=12)
        controls.rowconfigure(6, weight=1)
        self.status_text.insert("end", "Connect to COM5 to prove the USB CDC link is open. Packet lines will parse automatically.\n")
        self.status_text.configure(state="disabled")

    def _button(self, parent, text: str, command):
        button = tk.Button(parent, text=text, command=command, bg="#02080b", fg=TEXT, activebackground="#11191d", activeforeground=CYAN, relief="flat", padx=12, pady=7, font=("Consolas", 9))
        return button

    def _connect(self) -> None:
        if serial is None:
            self._log("pyserial is not installed; run deploy\\install_groundstation.ps1", "bad")
            return
        if self.serial is not None:
            self._log("Already connected", "warn")
            return

        port = self.port_var.get().strip()
        baud = int(self.baud_var.get().strip() or "115200")
        try:
            self.serial = serial.Serial(port=port, baudrate=baud, timeout=0.08)
        except Exception as exc:
            self.serial = None
            self._log(f"OPEN FAILED {port}@{baud}: {exc}", "bad")
            return

        self.opened_at = time.time()
        self.reader_running.set()
        self.reader_thread = threading.Thread(target=self._reader_loop, name="rangepi-viewer-reader", daemon=True)
        self.reader_thread.start()
        self._log(f"OPEN {port}@{baud}; USB serial link is online", "good")

    def _disconnect(self) -> None:
        self.reader_running.clear()
        if self.serial is not None:
            try:
                self.serial.close()
            except Exception:
                pass
        self.serial = None
        self._log("DISCONNECTED", "warn")

    def _reader_loop(self) -> None:
        while self.reader_running.is_set():
            current = self.serial
            if current is None:
                break
            try:
                waiting = current.in_waiting
                data = current.read(waiting if waiting else 1)
            except Exception as exc:
                self.rx_queue.put(("error", str(exc)))
                break
            if data:
                self.rx_queue.put(("data", data))
        self.reader_running.clear()

    def _send_raw(self, line: str) -> None:
        self._write_serial((line + "\n").encode("utf-8"), f"RAW {line!r}")

    def _send_command(self, command: str) -> None:
        try:
            session_id = int(self.session_var.get(), 0)
            dst_id = int(self.dst_var.get(), 0)
            self.tx_counter += 1
            raw = encode_command_packet(
                command_id=self.tx_counter,
                opcode=command_opcode_from_name(command),
                counter=self.tx_counter,
                session_id=session_id,
                timestamp=int(time.time()),
                src_id=2,
                dst_id=dst_id,
            )
            frame = b"TX " + raw.hex().upper().encode("ascii") + b"\n"
            self._write_serial(frame, f"TX {command} counter={self.tx_counter} bytes={len(raw)}")
        except Exception as exc:
            self._log(f"TX BUILD FAILED {command}: {exc}", "bad")

    def _write_serial(self, data: bytes, label: str) -> None:
        if self.serial is None:
            self._log("TX blocked: not connected", "bad")
            return
        try:
            self.serial.write(data)
            self.serial.flush()
            self.bytes_tx += len(data)
            self._log(label, "info")
        except Exception as exc:
            self._log(f"TX FAILED: {exc}", "bad")

    def _handle_rx_data(self, data: bytes) -> None:
        self.bytes_rx += len(data)
        self.last_rx_at = time.time()
        self.activity = min(1.0, self.activity + 0.35 + len(data) / 120.0)
        self.rx_history.append(min(100, len(data)))
        self.line_buffer.extend(data)

        while b"\n" in self.line_buffer:
            line, _, rest = self.line_buffer.partition(b"\n")
            self.line_buffer = bytearray(rest)
            self._handle_rx_line(line.rstrip(b"\r"))

        if len(self.line_buffer) >= 256:
            chunk = bytes(self.line_buffer)
            self.line_buffer.clear()
            self._log(f"RX BINARY {len(chunk)}B {chunk[:64].hex(' ').upper()}", "warn")

    def _handle_rx_line(self, line: bytes) -> None:
        if not line:
            return
        self.lines_rx += 1
        self._log(f"RX {self.lines_rx}: {_printable(line)}", "good")
        try:
            raw_packet = parse_rangepi_line(line)
            result = self.engine.handle_raw_packet(raw_packet)
            self.packets_rx += 1
            self._log(format_packet_result(result), "info" if result["accepted"] else "bad")
        except Exception as exc:
            self.parse_errors += 1
            self._log(f"not a CubeSat packet yet: {exc}", "muted")

    def _log(self, message: str, tag: str = "") -> None:
        stamp = time.strftime("%H:%M:%S")
        self.log.configure(state="normal")
        self.log.insert("end", f"[{stamp}] {message}\n", tag)
        line_count = int(self.log.index("end-1c").split(".")[0])
        if line_count > 500:
            self.log.delete("1.0", "80.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _ui_loop(self) -> None:
        while True:
            try:
                kind, payload = self.rx_queue.get_nowait()
            except queue.Empty:
                break
            if kind == "data" and isinstance(payload, bytes):
                self._handle_rx_data(payload)
            elif kind == "error":
                self._log(f"SERIAL ERROR: {payload}", "bad")
                self._disconnect()

        self.activity *= 0.88
        if len(self.rx_history) < self.rx_history.maxlen:
            self.rx_history.append(0)
        elif time.time() - self.last_rx_at > 0.8:
            self.rx_history.append(0)

        self._render_stats()
        self._render_static()
        self.after(80, self._ui_loop)

    def _render_stats(self) -> None:
        connected = self.serial is not None and getattr(self.serial, "is_open", False)
        idle = time.time() - self.last_rx_at if self.last_rx_at else 0.0
        self.stat_labels["USB"].config(text="OPEN" if connected else "CLOSED", fg=GREEN if connected else AMBER)
        self.stat_labels["RX BYTES"].config(text=f"{self.bytes_rx:,}")
        self.stat_labels["RX LINES"].config(text=f"{self.lines_rx:,}")
        self.stat_labels["PACKETS"].config(text=f"{self.packets_rx:,}")
        self.stat_labels["TX BYTES"].config(text=f"{self.bytes_tx:,}")
        self.stat_labels["IDLE"].config(text=f"{idle:0.1f}s" if connected else "--", fg=AMBER if connected and idle > 5 else TEXT)

        self.status_text.configure(state="normal")
        self.status_text.delete("1.0", "end")
        status = "USB CDC serial is open. Waiting for RangePi bridge output." if connected else "Disconnected."
        if connected and self.bytes_rx == 0:
            status += "\nNo bytes seen yet. This still proves the USB port opens; the bridge/radio side is idle or silent."
        elif connected:
            status += f"\nLast RX {idle:0.1f}s ago. Parse errors={self.parse_errors}."
        status += "\nExpected LoRa RX format: RX: <packet-hex> RSSI=-72 SNR=9.4"
        self.status_text.insert("end", status)
        self.status_text.configure(state="disabled")

    def _render_static(self) -> None:
        canvas = self.static_canvas
        width = max(canvas.winfo_width(), 1)
        height = max(canvas.winfo_height(), 1)
        canvas.delete("all")
        canvas.create_rectangle(0, 0, width, height, fill="#000304", outline="")
        canvas.create_text(12, 10, anchor="nw", fill=CYAN, text="SERIAL ENERGY / BRIDGE STATIC", font=("Consolas", 10, "bold"))

        baseline = 0.08 if self.serial is not None else 0.02
        level = min(1.0, baseline + self.activity)
        for _ in range(int(70 + level * 240)):
            x = random.randint(0, width)
            y = random.randint(34, max(35, height - 4))
            color = GREEN if random.random() < level * 0.16 else "#1c2b2f"
            if random.random() < 0.08 + level * 0.2:
                color = CYAN
            canvas.create_line(x, y, x + random.randint(1, 6), y, fill=color)

        graph_top = height - 58
        canvas.create_line(10, graph_top, width - 10, graph_top, fill=GRID)
        values = list(self.rx_history)
        if len(values) > 1:
            points = []
            for index, value in enumerate(values):
                px = 10 + (index / (len(values) - 1)) * (width - 20)
                py = height - 12 - (value / 100.0) * 40
                points.extend((px, py))
            canvas.create_line(*points, fill=GREEN if self.activity > 0.1 else AMBER, width=2)

        sweep = (time.time() * 70) % max(1, width)
        canvas.create_line(sweep, 32, sweep, height - 8, fill=RED if self.activity > 0.4 else "#203238")
        idle = time.time() - self.last_rx_at if self.last_rx_at else 0.0
        state = "RX ACTIVE" if idle < 1.0 and self.bytes_rx else "USB OPEN / RF IDLE" if self.serial is not None else "DISCONNECTED"
        canvas.create_text(width - 12, 10, anchor="ne", fill=GREEN if "ACTIVE" in state else AMBER, text=state, font=("Consolas", 10, "bold"))
        canvas.create_text(12, height - 12, anchor="sw", fill=MUTED, text=f"activity={level:.2f} packets={self.packets_rx} errors={self.parse_errors}", font=("Consolas", 9))

    def _on_close(self) -> None:
        self._disconnect()
        self.destroy()


def main() -> None:
    parser = argparse.ArgumentParser(description="RangePi USB serial and LoRa bridge viewer")
    parser.add_argument("--port", help="Serial port, e.g. COM5")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    app = RangePiViewer(port=args.port, baud=args.baud)
    app.mainloop()


if __name__ == "__main__":
    main()
