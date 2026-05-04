from groundstation.backend.event_engine import EventEngine
from groundstation.backend.packet_parser import encode_fake_packet


def main():
    engine = EventEngine()

    pkt1 = encode_fake_packet(counter=1)
    pkt1_duplicate = encode_fake_packet(counter=1)
    pkt2 = encode_fake_packet(counter=2)

    result1 = engine.handle_raw_packet(pkt1)
    result2 = engine.handle_raw_packet(pkt1_duplicate)
    result3 = engine.handle_raw_packet(pkt2)

    assert result1["accepted"] is True
    assert result2["accepted"] is False
    assert result3["accepted"] is True

    print("PASS: event engine accepts fresh packets and rejects replay")


if __name__ == "__main__":
    main()