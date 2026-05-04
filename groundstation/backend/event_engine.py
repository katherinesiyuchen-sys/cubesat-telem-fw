from groundstation.backend.packet_parser import decode_packet
from groundstation.backend.replay_filter import ReplayFilter


class EventEngine:
    def __init__(self):
        self.replay_filter = ReplayFilter()

    def handle_raw_packet(self, raw: bytes) -> dict:
        packet = decode_packet(raw)

        accepted = self.replay_filter.check_and_update(
            packet.session_id,
            packet.counter,
        )

        if not accepted:
            return {
                "accepted": False,
                "reason": "replay_or_stale_packet",
                "packet": packet,
            }

        return {
            "accepted": True,
            "reason": "ok",
            "packet": packet,
        }