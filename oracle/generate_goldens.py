#!/usr/bin/env python3
"""Generate or byte-verify immutable full-stage oracle goldens."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

from oracle import run


ROOT = Path(__file__).resolve().parents[1]
ORACLE_LOCK = ROOT / "oracle" / "oracle.lock.json"
ORACLE_SOURCE = ROOT / "oracle" / "oracle.py"
GOLDEN_LOCK = ROOT / "corpus" / "goldens.lock.json"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def golden_bytes(bundle: Path, fixture_path: Path) -> bytes:
    fixture = json.loads(fixture_path.read_text("utf-8"))
    pixels = fixture_path.parent / "pixels.bin"
    if sha256(pixels) != fixture["pixelSha256"]:
        raise RuntimeError(f"fixture pixel hash mismatch: {fixture['id']}")
    expected = run(
        bundle, pixels, fixture["width"], fixture["height"], fixture["stride"],
        fixture["pixelFormat"], include_crop_pixels=True,
    )
    return canonical({
        "schemaVersion": "1.0",
        "fixtureId": fixture["id"],
        "corpusRevision": fixture["corpusRevision"],
        "pixelSha256": fixture["pixelSha256"],
        "modelBundleId": expected["modelBundleId"],
        "oracleLockSha256": sha256(ORACLE_LOCK),
        "oracleSourceSha256": sha256(ORACLE_SOURCE),
        "expected": expected,
    })


def verify(bundle: Path, fixtures: Path, output: Path) -> None:
    lock = json.loads(GOLDEN_LOCK.read_text("utf-8"))
    if lock["oracleLockSha256"] != sha256(ORACLE_LOCK):
        raise RuntimeError("golden oracle lock identity is stale")
    if lock["oracleSourceSha256"] != sha256(ORACLE_SOURCE):
        raise RuntimeError("golden oracle source identity is stale")
    if lock["bundleManifestSha256"] != sha256(bundle / "manifest.json"):
        raise RuntimeError("golden bundle manifest identity is stale")
    records = {record["fixtureId"]: record for record in lock["fixtures"]}
    fixture_paths = sorted(fixtures.glob("*/fixture.json"))
    if set(records) != {path.parent.name for path in fixture_paths}:
        raise RuntimeError("golden fixture inventory does not match the materialized corpus")
    for fixture_path in fixture_paths:
        fixture_id = fixture_path.parent.name
        expected_bytes = golden_bytes(bundle, fixture_path)
        golden_path = output / records[fixture_id]["path"]
        if not golden_path.is_file() or golden_path.read_bytes() != expected_bytes:
            raise RuntimeError(f"golden cannot be reproduced byte-for-byte: {fixture_id}")
        if (len(expected_bytes) != records[fixture_id]["bytes"] or
                hashlib.sha256(expected_bytes).hexdigest() != records[fixture_id]["sha256"]):
            raise RuntimeError(f"golden lock mismatch: {fixture_id}")


def generate(bundle: Path, fixtures: Path, output: Path, force: bool) -> None:
    if output.exists() and not force:
        raise RuntimeError(f"golden output already exists: {output}; use --force to replace")
    temporary = Path(tempfile.mkdtemp(prefix="light-ocr-goldens-", dir=output.parent))
    try:
        records = []
        for fixture_path in sorted(fixtures.glob("*/fixture.json")):
            data = golden_bytes(bundle, fixture_path)
            relative = fixture_path.parent.name + ".json"
            (temporary / relative).write_bytes(data)
            records.append({"fixtureId": fixture_path.parent.name, "path": relative,
                            "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        if not records:
            raise RuntimeError("no fixtures found")
        lock = {
            "schemaVersion": "1.0",
            "oracleLockSha256": sha256(ORACLE_LOCK),
            "oracleSourceSha256": sha256(ORACLE_SOURCE),
            "bundleManifestSha256": sha256(bundle / "manifest.json"),
            "modelBundleId": json.loads((bundle / "manifest.json").read_text("utf-8"))["bundleId"],
            "fixtures": records,
        }
        lock_temporary = GOLDEN_LOCK.with_suffix(".json.tmp")
        lock_temporary.write_bytes(canonical(lock))
        if output.exists():
            shutil.rmtree(output)
        os.replace(temporary, output)
        os.replace(lock_temporary, GOLDEN_LOCK)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, default=ROOT / "corpus" / "fixtures")
    parser.add_argument("--output", type=Path, default=ROOT / "corpus" / "goldens")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--force", action="store_true")
    arguments = parser.parse_args()
    bundle = arguments.bundle.resolve()
    fixtures = arguments.fixtures.resolve()
    output = arguments.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if arguments.verify:
        verify(bundle, fixtures, output)
    else:
        generate(bundle, fixtures, output, arguments.force)
    print(json.dumps({"schemaVersion": "1.0", "verified": arguments.verify,
                      "goldenDirectory": str(output)}, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
