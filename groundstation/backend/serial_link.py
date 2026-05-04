from typing import Iterator, Optional

try:
    import serial
except ImportError:
    serial = None


class SerialLink:
    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 1.0):
        if serial is None:
            raise RuntimeError("pyserial is not installed. Run: pip install pyserial")

        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self._ser = None

    def open(self) -> None:
        self._ser = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            timeout=self.timeout,
        )

    def close(self) -> None:
        if self._ser is not None:
            self._ser.close()
            self._ser = None

    def read_line(self) -> Optional[bytes]:
        if self._ser is None:
            raise RuntimeError("Serial port is not open")

        line = self._ser.readline()
        if not line:
            return None

        return line.strip()

    def write_line(self, line: bytes | str) -> None:
        if self._ser is None:
            raise RuntimeError("Serial port is not open")

        payload = line.encode("utf-8") if isinstance(line, str) else line
        if not payload.endswith(b"\n"):
            payload += b"\n"
        self._ser.write(payload)
        self._ser.flush()

    def lines(self) -> Iterator[bytes]:
        while True:
            line = self.read_line()
            if line:
                yield line
