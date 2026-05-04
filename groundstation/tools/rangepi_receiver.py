import argparse

from groundstation.backend.event_engine import EventEngine
from groundstation.backend.rangepi import format_packet_result, parse_rangepi_line

# This is a simple tool to read lines from the RangePi serial output, parse them as packets, and print the decoded packet contents
def main():
    parser = argparse.ArgumentParser(description="Read LoRa packets from RangePi over serial.")
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        raise SystemExit("pyserial is not installed. Run: pip install pyserial")

    engine = EventEngine()

    print(f"Opening RangePi serial port {args.port} at {args.baud} baud")

    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        while True:
            line = ser.readline()

            if not line:
                continue

            print(f"RAW: {line!r}")

            try:
                raw_packet = parse_rangepi_line(line)
                result = engine.handle_raw_packet(raw_packet)
                print(format_packet_result(result))

            except Exception as e:
                print(f"Could not parse packet: {e}")


if __name__ == "__main__":
    main()
