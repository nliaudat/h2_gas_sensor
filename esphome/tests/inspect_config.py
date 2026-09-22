"""Debug helper: print the resolved `id` of the MQ sensor of a config file.

Usage: python tests/inspect_config.py tests/test_no_id.yaml
"""

import sys
from pathlib import Path

import esphome.config as cfg
from esphome.core import CORE


def main() -> int:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "tests/test_no_id.yaml")
    CORE.config_path = path
    config = cfg.read_config(path)
    for entry in config.get("sensor", []):
        if entry.get("platform") == "MQ_gas_sensors":
            print(f"keys : {sorted(entry)}")
            print(f"id   : {entry.get('id')!r}")
            print(f"type : {entry.get('id').__class__.__name__}")
            print(f"name : {entry.get('name')!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
