from groundstation.backend.packet_parser import decode_packet
from groundstation.backend.replay_filter import ReplayFilter

class EventEngine:
    def __init__(self):
        self.replay_filter = ReplayFilter()

    def handle_raw_packet(self, raw: bytes):
        pkt = decode_packet(raw)

        accepted = self.replay_filter.check_and_update(
            pkt.session_id,
            pkt.counter,
        )

        if not accepted:
            return {
                "accepted": False,
                "reason": "replay_or_stale_packet",
                "packet": pkt,
            }

        return {
            "accepted": True,
            "reason": "ok",
            "packet": pkt,
        }