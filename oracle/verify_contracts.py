#!/usr/bin/env python3
"""Verify that every non-pixel corpus contract names executable test evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contracts", type=Path, default=ROOT / "corpus" / "contracts.json")
    parser.add_argument("--sources-lock", type=Path,
                        default=ROOT / "corpus" / "sources.lock.json")
    arguments = parser.parse_args()
    contracts = json.loads(arguments.contracts.read_text("utf-8"))
    sources = json.loads(arguments.sources_lock.read_text("utf-8"))
    if contracts.get("schemaVersion") != "1.0":
        raise RuntimeError("unsupported contract manifest schema")
    if contracts.get("corpusRevision") != sources.get("revision"):
        raise RuntimeError("contract manifest and corpus source revisions differ")

    evidence = {
        "light_ocr_unit_tests": "\n".join(
            path.read_text("utf-8") for path in sorted((ROOT / "tests" / "unit").glob("*.cpp"))
        ),
        "light_ocr_integration_tests": (ROOT / "tests" / "integration" / "main.cpp").read_text(
            "utf-8"
        ),
    }
    records = contracts.get("fixtures")
    if not isinstance(records, list) or not records:
        raise RuntimeError("contract manifest has no fixtures")
    identifiers = [record.get("id") for record in records]
    if not all(isinstance(value, str) and value for value in identifiers):
        raise RuntimeError("contract fixture ID is invalid")
    if len(set(identifiers)) != len(identifiers):
        raise RuntimeError("contract fixture IDs are not unique")
    for record in records:
        target = record.get("testTarget")
        test = record.get("test")
        if target not in evidence or not isinstance(test, str) or not test:
            raise RuntimeError(f"contract test mapping is invalid: {record['id']}")
        if test not in evidence[target]:
            raise RuntimeError(f"contract test evidence is stale: {record['id']} -> {test}")
        if "expected" not in record and "expectedError" not in record:
            raise RuntimeError(f"contract has no expected outcome: {record['id']}")
    print(json.dumps({"schemaVersion": "1.0", "passed": True,
                      "contractCount": len(records)}, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
