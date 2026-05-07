from pathlib import Path
from tempfile import TemporaryDirectory

from groundstation.backend.node_registry import NodeRegistry


def main() -> None:
    with TemporaryDirectory() as tmpdir:
        registry_path = Path(tmpdir) / "state" / "nodes.json"
        registry = NodeRegistry(registry_path)

        assert registry.load() == []

        nodes = [
            {
                "name": "TEST-NODE",
                "role": "bench",
                "session_id": 0xAABBCCDD,
                "fixed_latitude": 37.7749,
                "fixed_longitude": -122.4194,
                "crypto_state": "ML-KEM READY",
                "radio_id": 42,
            }
        ]
        registry.save(nodes)

        loaded = registry.load()
        assert loaded == nodes
        assert registry_path.exists()

    print("PASS: node registry")


if __name__ == "__main__":
    main()
