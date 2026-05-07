import json
import threading
from http.client import HTTPConnection
from pathlib import Path

from groundstation.ui.mastercontrol_web import MissionControlCore, _bind_server, _server_class


def main() -> None:
    core = MissionControlCore(simulate=True, rangepi_port="sim://test")
    snapshot = core.snapshot()
    assert snapshot["nodes"]
    assert snapshot["selected_node"]["name"] == snapshot["selected"]

    result = core.execute("ping")
    assert result["ok"]
    assert "telemetry" in " ".join(result["lines"])
    snapshot = core.snapshot()
    assert snapshot["crypto_rx"]
    assert snapshot["crypto_rx"][-1]["ciphertext_hex"]

    result = core.execute("use all")
    assert result["ok"]
    assert core.command_target == "ALL"

    result = core.execute("tx ping SF-MISSION")
    assert result["ok"]
    assert "SF-MISSION" in " ".join(result["lines"])

    result = core.execute("rx latest")
    assert result["ok"]
    assert "ciphertext" in " ".join(result["lines"])

    result = core.execute("addnode LAB-ESP32 esp32-node lat=37.76 lon=-122.40 radio=42 session=0xABCDEF01")
    assert result["ok"]
    assert "radio=42" in " ".join(result["lines"])
    staged = next(node for node in core.snapshot()["nodes"] if node["name"] == "LAB-ESP32")
    assert staged["radio_id"] == 42
    assert staged["session"] == "0xABCDEF01"

    result = core.execute("addnode RADIO-DUPE sensor-node radio=42")
    assert result["ok"]
    assert "already assigned" in " ".join(result["lines"])

    result = core.execute("delnode LAB-ESP32")
    assert result["ok"]
    assert all(node["name"] != "LAB-ESP32" for node in core.snapshot()["nodes"])

    result = core.execute("clear")
    assert result["ok"]

    handler = _server_class(core, Path("groundstation/ui/web"))
    server = _bind_server("127.0.0.1", 0, handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        conn = HTTPConnection("127.0.0.1", server.server_address[1], timeout=5)
        conn.request("GET", "/api/state")
        response = conn.getresponse()
        assert response.status == 200
        body = json.loads(response.read().decode("utf-8"))
        assert body["nodes"]
        assert "crypto_rx" in body

        conn.request("GET", "/")
        response = conn.getresponse()
        assert response.status == 200
        html = response.read()
        assert b"CubeSat Master Control" in html
        assert b"RX Crypto" in html

        conn.request("GET", "/app.js")
        response = conn.getresponse()
        assert response.status == 200
        assert b"drawMap" in response.read()

        conn.request(
            "POST",
            "/api/command",
            body=json.dumps({"command": "status"}),
            headers={"Content-Type": "application/json"},
        )
        response = conn.getresponse()
        assert response.status == 200
        body = json.loads(response.read().decode("utf-8"))
        assert body["ok"]
    finally:
        server.shutdown()
        server.server_close()
        core.stop()

    print("PASS: mastercontrol web")


if __name__ == "__main__":
    main()
