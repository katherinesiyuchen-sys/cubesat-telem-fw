from groundstation.backend.packet_parser import HOPE_PACKET_TYPE_HANDSHAKE, decode_packet, encode_handshake_packet
from groundstation.models.lattice import (
    LATTICE_FRAGMENT_DATA_MAX,
    LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE,
    LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY,
    MLDSA44_SIGNATURE_LEN,
    LatticeReassembler,
    build_fragment_payload,
    fragment_count,
    parse_fragment_payload,
)


def test_fragment_round_trip() -> None:
    obj = bytes((index * 13) & 0xFF for index in range(800))
    count = fragment_count(len(obj))
    assert count > 1

    reassembler = LatticeReassembler()
    complete = None
    for index in reversed(range(count)):
        payload = build_fragment_payload(LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY, 77, index, obj)
        fragment = parse_fragment_payload(payload)
        assert len(fragment.fragment) <= LATTICE_FRAGMENT_DATA_MAX
        complete = reassembler.add(fragment)

    assert complete == obj


def test_handshake_packet_encode_decode() -> None:
    obj = b"lattice-session-confirm"
    raw = encode_handshake_packet(
        message_type=LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY,
        transfer_id=12,
        fragment_index=0,
        obj=obj,
        counter=99,
        session_id=0xA1B2C3D4,
    )
    packet = decode_packet(raw)
    fragment = parse_fragment_payload(packet.payload)

    assert packet.packet_type == HOPE_PACKET_TYPE_HANDSHAKE
    assert packet.counter == 99
    assert packet.session_id == 0xA1B2C3D4
    assert fragment.transfer_id == 12
    assert fragment.fragment == obj


def test_signature_object_fragment_count() -> None:
    signature = bytes((index * 19) & 0xFF for index in range(MLDSA44_SIGNATURE_LEN))
    count = fragment_count(len(signature))
    assert count > 20

    payload = build_fragment_payload(LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE, 33, count - 1, signature)
    fragment = parse_fragment_payload(payload)
    assert fragment.message_type == LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE
    assert fragment.fragment_index == count - 1


def main() -> None:
    test_fragment_round_trip()
    test_handshake_packet_encode_decode()
    test_signature_object_fragment_count()
    print("PASS: lattice protocol")


if __name__ == "__main__":
    main()
