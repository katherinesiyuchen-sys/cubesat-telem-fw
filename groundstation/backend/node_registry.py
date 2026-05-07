import json
import time
from pathlib import Path
from typing import Any


REGISTRY_VERSION = 1
DEFAULT_NODE_REGISTRY_PATH = Path(__file__).resolve().parents[1] / "state" / "nodes.json"


class NodeRegistry:
    def __init__(self, path: Path | None = None) -> None:
        self.path = path or DEFAULT_NODE_REGISTRY_PATH

    def load(self) -> list[dict[str, Any]]:
        if not self.path.exists():
            return []
        with self.path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
        if not isinstance(data, dict) or data.get("version") != REGISTRY_VERSION:
            raise ValueError(f"unsupported node registry format: {self.path}")
        nodes = data.get("nodes", [])
        if not isinstance(nodes, list):
            raise ValueError("node registry nodes field must be a list")
        return [node for node in nodes if isinstance(node, dict)]

    def save(self, nodes: list[dict[str, Any]]) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "version": REGISTRY_VERSION,
            "updated_at": int(time.time()),
            "nodes": nodes,
        }
        tmp_path = self.path.with_suffix(".tmp")
        with tmp_path.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, sort_keys=True)
            handle.write("\n")
        tmp_path.replace(self.path)
