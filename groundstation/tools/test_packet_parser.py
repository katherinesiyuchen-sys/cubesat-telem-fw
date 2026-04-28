from groundstation.backend.packet_parser import decode_packet, encode_fake_packet

def main():
    raw = encode_fake_packet(counter=7)
    pkt = decode_packet(raw)

    print("Decoded packet:")
    print(f"  version:    {pkt.version}")
    print(f"  type:       {pkt.packet_type}")
    print(f"  src:        {pkt.src_id}")
    print(f"  dst:        {pkt.dst_id}")
    print(f"  session:    0x{pkt.session_id:08X}")
    print(f"  counter:    {pkt.counter}")
    print(f"  timestamp:  {pkt.timestamp}")
    print(f"  payload:    {pkt.payload.decode()}")

    assert pkt.version == 1
    assert pkt.packet_type == 1
    assert pkt.src_id == 1
    assert pkt.dst_id == 2
    assert pkt.session_id == 0x12345678
    assert pkt.counter == 7

    print("PASS: packet parser")

if __name__ == "__main__":
    main()