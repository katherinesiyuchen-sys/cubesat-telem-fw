## A simple replay filter that tracks the largest seen counter for each session and rejects packets with counters that are not strictly increasing.

class ReplayFilter:
    def __init__(self):
        self._largest_seen_by_session = {}

## Returns True if packet is accepted, false if rejected as a replay.
    def check_and_update(self, session_id: int, counter: int) -> bool:
        if counter == 0:
            return False

        largest_seen = self._largest_seen_by_session.get(session_id, 0)

        if counter <= largest_seen:
            return False

        self._largest_seen_by_session[session_id] = counter
        return True