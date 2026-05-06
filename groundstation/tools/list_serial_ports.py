try:
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - exercised before pyserial install
    list_ports = None


def serial_port_rows() -> list[tuple[str, str, str]]:
    if list_ports is None:
        raise RuntimeError("pyserial is not installed. Run deploy/install_groundstation.ps1 first.")
    return [
        (port.device, port.description or "-", port.hwid or "-")
        for port in list_ports.comports()
    ]


def print_serial_ports() -> None:
    print("Available serial ports")
    print("----------------------")
    rows = serial_port_rows()
    if not rows:
        print("No serial ports found. Plug in the RangePi USB bridge and run again.")
    for device, description, hwid in rows:
        print(f"{device:<12} {description:<38} {hwid}")
    print()
    print("Simulator port: sim://cubesat")


def main() -> None:
    print_serial_ports()


if __name__ == "__main__":
    main()
