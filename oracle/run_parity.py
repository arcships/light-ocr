#!/usr/bin/env python3
"""Run one immutable fixture through the native probe and pinned Python oracle."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from typing import Any

from compare import compare
from oracle import run


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-probe", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--native-record", type=Path,
                        help="optional canonical native stage record for diagnostics")
    parser.add_argument("--oracle-record", type=Path,
                        help="optional canonical oracle stage record for diagnostics")
    parser.add_argument("--live-oracle", action="store_true",
                        help="diagnostic mode; compare with a fresh oracle run instead of locked golden")
    arguments = parser.parse_args()

    fixture = json.loads(arguments.fixture.read_text("utf-8"))
    pixels = arguments.fixture.parent / "pixels.bin"
    if file_sha256(pixels) != fixture["pixelSha256"]:
        raise RuntimeError("fixture pixel SHA-256 does not match fixture.json")
    common = [
        "--bundle",
        str(arguments.bundle),
        "--pixels",
        str(pixels),
        "--width",
        str(fixture["width"]),
        "--height",
        str(fixture["height"]),
        "--stride",
        str(fixture["stride"]),
        "--format",
        fixture["pixelFormat"],
    ]
    native_process = subprocess.run(
        [str(arguments.native_probe), *common],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if native_process.returncode != 0:
        raise RuntimeError(
            f"native stage probe failed ({native_process.returncode}): "
            f"{native_process.stdout}{native_process.stderr}"
        )
    native = json.loads(native_process.stdout)
    if arguments.live_oracle:
        oracle = run(
            arguments.bundle,
            pixels,
            fixture["width"],
            fixture["height"],
            fixture["stride"],
            fixture["pixelFormat"],
            include_crop_pixels=True,
        )
        oracle_lock_sha256 = file_sha256(Path(__file__).with_name("oracle.lock.json"))
    else:
        corpus_root = arguments.fixture.parents[2]
        golden_lock = json.loads((corpus_root / "goldens.lock.json").read_text("utf-8"))
        if golden_lock["bundleManifestSha256"] != file_sha256(arguments.bundle / "manifest.json"):
            raise RuntimeError("oracle goldens were generated for a different bundle manifest")
        if golden_lock["oracleLockSha256"] != file_sha256(Path(__file__).with_name("oracle.lock.json")):
            raise RuntimeError("oracle golden environment lock is stale")
        if golden_lock["oracleSourceSha256"] != file_sha256(Path(__file__).with_name("oracle.py")):
            raise RuntimeError("oracle golden source identity is stale")
        record = next(
            (item for item in golden_lock["fixtures"] if item["fixtureId"] == fixture["id"]),
            None,
        )
        if record is None:
            raise RuntimeError(f"fixture has no locked oracle golden: {fixture['id']}")
        golden_path = corpus_root / "goldens" / record["path"]
        golden_bytes = golden_path.read_bytes()
        if len(golden_bytes) != record["bytes"] or hashlib.sha256(golden_bytes).hexdigest() != record["sha256"]:
            raise RuntimeError(f"oracle golden lock mismatch: {fixture['id']}")
        golden = json.loads(golden_bytes)
        if (golden["fixtureId"] != fixture["id"] or
                golden["corpusRevision"] != fixture["corpusRevision"] or
                golden["pixelSha256"] != fixture["pixelSha256"] or
                golden["modelBundleId"] != golden_lock["modelBundleId"]):
            raise RuntimeError(f"oracle golden identity mismatch: {fixture['id']}")
        oracle = golden["expected"]
        oracle_lock_sha256 = golden["oracleLockSha256"]
    exception_path = arguments.fixture.parents[2] / "parity-exceptions.json"
    exceptions = []
    if exception_path.is_file():
        all_exceptions = json.loads(exception_path.read_text("utf-8"))["exceptions"]
        exceptions = [
            record for record in all_exceptions
            if record["fixtureId"] == fixture["id"] and record["platform"] in ("all", sys.platform)
        ]
    report: dict[str, Any] = compare(native, oracle, exceptions)
    report.update(
        {
            "fixtureId": fixture["id"],
            "corpusRevision": fixture["corpusRevision"],
            "pixelSha256": fixture["pixelSha256"],
            "oracleLockSha256": oracle_lock_sha256,
            "oracleMode": "live" if arguments.live_oracle else "locked-golden",
            "nativeProbe": {
                "filename": arguments.native_probe.name,
                "sha256": file_sha256(arguments.native_probe),
            },
        }
    )
    expected_text = fixture.get("groundTruth", {}).get(
        "lines", fixture.get("generation", {}).get("expectedText")
    )
    if expected_text is not None:
        observed_text = [line["text"] for line in native["lines"]]
        expected_passed = observed_text == expected_text
        report["groundTruth"] = {
            "exactLineMatch": expected_passed,
            "observed": observed_text,
            "expected": expected_text,
            "note": "First-bundle quality baseline; not a parity release gate.",
        }
    serialized = json.dumps(
        report, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ) + "\n"
    for path, record in ((arguments.native_record, native),
                         (arguments.oracle_record, oracle)):
        if path:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                json.dumps(record, ensure_ascii=False, sort_keys=True,
                           separators=(",", ":")) + "\n",
                encoding="utf-8",
            )
    if arguments.report:
        arguments.report.parent.mkdir(parents=True, exist_ok=True)
        arguments.report.write_text(serialized, encoding="utf-8")
    sys.stdout.write(serialized)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
