import time

from groundstation.backend.event_engine import EventEngine
from groundstation.backend.packet_parser import encode_fake_packet
from groundstation.models.telemetry import parse_telemetry_payload


def main():
    engine = EventEngine()

    counters = [1, 2, 2, 3, 1, 4]

    for counter in counters:
        raw = encode_fake_packet(counter=counter)
        result = engine.handle_raw_packet(raw)
        packet = result["packet"]
        telemetry = parse_telemetry_payload(packet.payload)

        status = "ACCEPTED" if result["accepted"] else "REJECTED"

        print(
            f"{status} "
            f"src={packet.src_id} "
            f"dst={packet.dst_id} "
            f"session=0x{packet.session_id:08X} "
            f"counter={packet.counter} "
            f"lat={telemetry.latitude:.5f} "
            f"lon={telemetry.longitude:.5f} "
            f"sats={telemetry.satellites}"
        )

        time.sleep(0.5)


if __name__ == "__main__":
    main()
