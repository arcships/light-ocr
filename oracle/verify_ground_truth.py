#!/usr/bin/env python3
"""CLI verifier for the immutable ground-truth lock."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from ground_truth import verify_ground_truth


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", type=Path, default=ROOT / "corpus" / "fixtures")
    parser.add_argument("--lock", type=Path, default=ROOT / "corpus" / "ground-truth.lock.json")
    arguments = parser.parse_args()
    records = verify_ground_truth(arguments.fixtures.resolve(), arguments.lock.resolve())
    print(json.dumps({"schemaVersion": "1.0", "passed": True,
                      "fixtureCount": len(records)}, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
