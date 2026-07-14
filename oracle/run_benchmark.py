#!/usr/bin/env python3
"""Run and gate native C++ performance against the pinned Python oracle."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from benchmark import benchmark


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-benchmark", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--iterations", type=int, default=30)
    parser.add_argument("--report", type=Path)
    arguments = parser.parse_args()

    fixture = json.loads(arguments.fixture.read_text("utf-8"))
    pixels = arguments.fixture.parent / "pixels.bin"
    if file_sha256(pixels) != fixture["pixelSha256"]:
        raise RuntimeError("fixture pixel SHA-256 does not match fixture.json")
    common = [
        "--bundle", str(arguments.bundle), "--pixels", str(pixels),
        "--width", str(fixture["width"]), "--height", str(fixture["height"]),
        "--stride", str(fixture["stride"]), "--format", fixture["pixelFormat"],
        "--warmup", str(arguments.warmup), "--iterations", str(arguments.iterations),
    ]
    native_process = subprocess.run(
        [str(arguments.native_benchmark), *common],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if native_process.returncode != 0:
        raise RuntimeError(
            f"native benchmark failed ({native_process.returncode}): "
            f"{native_process.stdout}{native_process.stderr}"
        )
    native = json.loads(native_process.stdout)
    oracle = benchmark(
        arguments.bundle, pixels, fixture["width"], fixture["height"],
        fixture["stride"], fixture["pixelFormat"], arguments.warmup,
        arguments.iterations,
    )
    ratios = {
        "warmMedian": native["latencyUs"]["median"] / oracle["latencyUs"]["median"],
        "warmP95": native["latencyUs"]["p95"] / oracle["latencyUs"]["p95"],
        "inferenceOnlyMedian": native["inferenceOnlyUs"]["median"]
        / oracle["inferenceOnlyUs"]["median"],
    }
    gates = {
        "warmMedian": {"maximumRatio": 1.10, "observedRatio": ratios["warmMedian"]},
        "warmP95": {"maximumRatio": 1.15, "observedRatio": ratios["warmP95"]},
        "inferenceOnlyMedian": {
            "maximumRatio": 1.05,
            "observedRatio": ratios["inferenceOnlyMedian"],
        },
    }
    passed = all(value["observedRatio"] <= value["maximumRatio"] for value in gates.values())
    report = {
        "schemaVersion": "1.0",
        "passed": passed,
        "fixtureId": fixture["id"],
        "pixelSha256": fixture["pixelSha256"],
        "oracleLockSha256": file_sha256(Path(__file__).with_name("oracle.lock.json")),
        "testOrder": [fixture["id"]],
        "native": native,
        "oracle": oracle,
        "gates": gates,
    }
    serialized = json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n"
    if arguments.report:
        arguments.report.parent.mkdir(parents=True, exist_ok=True)
        arguments.report.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
