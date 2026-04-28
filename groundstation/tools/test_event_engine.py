from groundstation.backend.event_engine import EventEngine
from groundstation.backend.packet_parser import encode_fake_packet

def main():
    engine = EventEngine()

    pkt1 = encode_fake_packet(counter=1)
    pkt1_dup = encode_fake_packet(counter=1)
    pkt2 = encode_fake_packet(counter=2)

    r1 = engine.handle_raw_packet(pkt1)
    r2 = engine.handle_raw_packet(pkt1_dup)
    r3 = engine.handle_raw_packet(pkt2)

    assert r1["accepted"] is True
    assert r2["accepted"] is False
    assert r3["accepted"] is True

    print("PASS: event engine accepts fresh packets and rejects replay")

if __name__ == "__main__":
    main()