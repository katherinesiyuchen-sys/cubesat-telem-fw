import argparse
from dataclasses import dataclass, replace
import time

from groundstation.backend.event_engine import EventEngine
from groundstation.backend.packet_parser import encode_telemetry_packet
from groundstation.backend.rangepi import format_packet_result, parse_rangepi_line
from groundstation.backend.serial_link import SerialLink


@dataclass
class DemoPoint:
    counter: int
    latitude: float
    longitude: float
    temperature_c: float
    fix_type: int
    satellites: int
    note: str


def print_result(result: dict) -> None:
    print(format_packet_result(result))


def demo_pass_points() -> list[DemoPoint]:
    start_lat = 37.8715
    start_lon = -122.2730
    points = []

    for index in range(10):
        points.append(
            DemoPoint(
                counter=index + 1,
                latitude=start_lat + (index * 0.0184),
                longitude=start_lon + (index * 0.0271),
                temperature_c=22.8 + (index * 0.35),
                fix_type=3,
                satellites=7 + (index % 4),
                note="fresh",
            )
        )

    points.insert(4, replace(points[2], note="replay duplicate"))
    points.insert(8, replace(points[1], note="stale replay"))
    return points

def run_fake_mode(delay: float) -> None:
    engine = EventEngine()

    print("Starting CubeSat groundstation in fake mode")
    print("Press Ctrl+C to stop\n")

    for point in demo_pass_points():
        raw = encode_telemetry_packet(
            counter=point.counter,
            latitude=point.latitude,
            longitude=point.longitude,
            temperature_c=point.temperature_c,
            fix_type=point.fix_type,
            satellites=point.satellites,
            timestamp=point.counter * 2,
        )
        result = engine.handle_raw_packet(raw)
        print(f"{point.note:<18}", end="")
        print_result(result)
        time.sleep(delay)


def run_serial_mode(port: str, baud: int) -> None:
    engine = EventEngine()
    link = SerialLink(port=port, baudrate=baud)

    print(f"Starting CubeSat groundstation in serial mode")
    print(f"Opening {port} at {baud} baud")
    print("Press Ctrl+C to stop\n")

    link.open()

    try:
        for line in link.lines():
            print(f"RAW: {line!r}")

            try:
                raw_packet = parse_rangepi_line(line)
                result = engine.handle_raw_packet(raw_packet)
                print_result(result)
            except Exception as e:
                print(f"PARSE_ERROR: {e}")

    finally:
        link.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="CubeSat Groundstation")

    parser.add_argument(
        "--mode",
        choices=["fake", "serial"],
        default="fake",
    )

    parser.add_argument(
        "--delay",
        type=float,
        default=0.5,
        help="Delay between fake packets.",
    )

    parser.add_argument(
        "--port",
        help="Serial port for RangePi, e.g. COM5",
    )

    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
    )

    args = parser.parse_args()

    if args.mode == "fake":
        run_fake_mode(args.delay)
    elif args.mode == "serial":
        if not args.port:
            raise SystemExit("serial mode requires --port, e.g. --port COM5")
        run_serial_mode(args.port, args.baud)


if __name__ == "__main__":
    main()
