from groundstation.backend.replay_filter import ReplayFilter

def main():
    rf = ReplayFilter()

    session = 0x12345678

    assert rf.check_and_update(session, 0) is False
    assert rf.check_and_update(session, 1) is True
    assert rf.check_and_update(session, 1) is False
    assert rf.check_and_update(session, 2) is True
    assert rf.check_and_update(session, 1) is False

    other_session = 0xAABBCCDD
    assert rf.check_and_update(other_session, 1) is True

    print("PASS: replay filter")

if __name__ == "__main__":
    main()