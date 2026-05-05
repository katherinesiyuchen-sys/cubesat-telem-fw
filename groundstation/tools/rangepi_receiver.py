import argparse
import binascii
import queue
import sys
import threading
import time

from groundstation.backend.event_engine import EventEngine
from groundstation.backend.packet_parser import encode_command_packet
from groundstation.backend.rangepi import format_packet_result, parse_rangepi_line
from groundstation.backend.serial_link import SerialLink
from groundstation.models.command import command_opcode_from_name


def _write_tx_frame(link: SerialLink, raw_packet: bytes) -> None:
    link.write_line(b"TX " + raw_packet.hex().upper().encode("ascii"))


def _stdin_worker(outbox: queue.Queue[str]) -> None:
    for line in sys.stdin:
        outbox.put(line.strip())


def _build_command_packet(args, command: str, counter: int) -> bytes:
    return encode_command_packet(
        command_id=counter,
        opcode=command_opcode_from_name(command),
        counter=counter,
        session_id=args.session,
        timestamp=int(time.time()),
        src_id=args.src_id,
        dst_id=args.dst_id,
    )


def _handle_interactive_line(link: SerialLink, args, line: str, counter_state: dict[str, int]) -> bool:
    if not line:
        return True
    if line.lower() in {"quit", "exit"}:
        return False

    parts = line.split(maxsplit=1)
    verb = parts[0].lower()
    rest = parts[1] if len(parts) > 1 else ""

    try:
        if verb == "tx":
            compact = "".join(ch for ch in rest if ch not in " :-,\t")
            raw = binascii.unhexlify(compact)
            _write_tx_frame(link, raw)
            print(f"TX raw bytes={len(raw)}")
            return True

        if verb == "cmd":
            if not rest:
                print("usage: cmd <selftest|ping|downlink|pause|resume|rotate|isolate|connect|arm>")
                return True
            counter_state["counter"] += 1
            raw = _build_command_packet(args, rest.strip(), counter_state["counter"])
            _write_tx_frame(link, raw)
            print(f"TX command={rest.strip()} counter={counter_state['counter']} bytes={len(raw)}")
            return True

        if verb == "raw":
            link.write_line(rest)
            print(f"RAW serial> {rest}")
            return True

        print("commands: tx <hex> | cmd <name> | raw <serial-line> | quit")
    except Exception as exc:
        print(f"TX failed: {exc}")

    return True


def main():
    parser = argparse.ArgumentParser(description="Bidirectional RangePi LoRa serial bridge.")
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM5, /dev/ttyUSB0, or sim://cubesat")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--interactive", action="store_true", help="Read TX/cmd commands from stdin while receiving")
    parser.add_argument("--send-hex", help="Send one raw packet as hex using the TX <hex> bridge command")
    parser.add_argument("--send-command", help="Send one framed CubeSat command packet")
    parser.add_argument("--exit-after-send", action="store_true", help="Exit after --send-hex or --send-command")
    parser.add_argument("--session", type=lambda value: int(value, 0), default=0x12345678)
    parser.add_argument("--counter", type=lambda value: int(value, 0), default=1)
    parser.add_argument("--src-id", type=int, default=2)
    parser.add_argument("--dst-id", type=int, default=1)
    args = parser.parse_args()

    engine = EventEngine()
    stdin_queue: queue.Queue[str] = queue.Queue()
    counter_state = {"counter": args.counter - 1}

    print(f"Opening RangePi serial port {args.port} at {args.baud} baud")
    print("Bridge TX format: TX <packet-hex>")

    link = SerialLink(args.port, args.baud, timeout=0.2)
    link.open()
    try:
        sent_one_shot = False
        if args.send_hex:
            _write_tx_frame(link, binascii.unhexlify(args.send_hex))
            sent_one_shot = True
            print("TX one-shot raw packet")
        if args.send_command:
            counter_state["counter"] += 1
            raw = _build_command_packet(args, args.send_command, counter_state["counter"])
            _write_tx_frame(link, raw)
            sent_one_shot = True
            print(f"TX one-shot command={args.send_command} counter={counter_state['counter']}")
        if sent_one_shot and args.exit_after_send:
            return

        if args.interactive:
            threading.Thread(target=_stdin_worker, args=(stdin_queue,), daemon=True).start()
            print("Interactive commands: tx <hex> | cmd <name> | raw <serial-line> | quit")

        running = True
        while running:
            line = link.read_line()
            if line:
                print(f"RAW: {line!r}")
                try:
                    raw_packet = parse_rangepi_line(line)
                    result = engine.handle_raw_packet(raw_packet)
                    print(format_packet_result(result))
                except Exception as exc:
                    print(f"Could not parse packet: {exc}")

            while running:
                try:
                    user_line = stdin_queue.get_nowait()
                except queue.Empty:
                    break
                running = _handle_interactive_line(link, args, user_line, counter_state)
    finally:
        link.close()


if __name__ == "__main__":
    main()
