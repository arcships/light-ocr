#!/usr/bin/env python3
"""Gate native tiled-v1 latency against the independent Python oracle."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

import psutil

from tiled_benchmark import benchmark


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-benchmark", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--platform-id", required=True)
    parser.add_argument("--build-metadata", type=Path, required=True)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--report", type=Path, required=True)
    arguments = parser.parse_args()
    if arguments.warmup != 5 or arguments.iterations != 10:
        raise RuntimeError("release tiled benchmarks require warmup=5 and iterations=10")

    fixture = json.loads(arguments.fixture.read_text("utf-8"))
    build_metadata = json.loads(arguments.build_metadata.read_text("utf-8"))
    if build_metadata.get("platformId") != arguments.platform_id:
        raise RuntimeError("build metadata platform does not match benchmark platform")
    pixels = arguments.fixture.parent / "pixels.bin"
    if sha256(pixels) != fixture["pixelSha256"]:
        raise RuntimeError("fixture pixel SHA-256 does not match fixture.json")
    common = [
        "--bundle", str(arguments.bundle),
        "--pixels", str(pixels),
        "--width", str(fixture["width"]),
        "--height", str(fixture["height"]),
        "--stride", str(fixture["stride"]),
        "--format", fixture["pixelFormat"],
        "--warmup", str(arguments.warmup),
        "--iterations", str(arguments.iterations),
        "--profile", "tiled_v1",
    ]
    native_process = subprocess.run(
        [str(arguments.native_benchmark), *common],
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=120 * (arguments.warmup + arguments.iterations),
    )
    if native_process.returncode != 0:
        raise RuntimeError(
            f"native tiled benchmark failed ({native_process.returncode}): "
            f"{native_process.stdout}{native_process.stderr}"
        )
    native = json.loads(native_process.stdout)
    oracle = benchmark(
        arguments.bundle,
        pixels,
        fixture["width"],
        fixture["height"],
        fixture["stride"],
        fixture["pixelFormat"],
        arguments.warmup,
        arguments.iterations,
    )
    ratios = {
        "warmMedian": native["latencyUs"]["median"] / oracle["latencyUs"]["median"],
        "warmP95": native["latencyUs"]["p95"] / oracle["latencyUs"]["p95"],
        "inferenceOnlyMedian": native["inferenceOnlyUs"]["median"]
        / oracle["inferenceOnlyUs"]["median"],
    }
    gates = {
        "sampleCount": {
            "expected": 10,
            "native": native["iterations"],
            "oracle": oracle["iterations"],
        },
        "perCallTimeout": {
            "maximumUs": 120_000_000,
            "nativeMaximumUs": native["latencyUs"]["maximum"],
            "oracleMaximumUs": oracle["latencyUs"]["maximum"],
        },
    }
    passed = (
        native["iterations"] == oracle["iterations"] == 10
        and native["latencyUs"]["maximum"] < 120_000_000
        and oracle["latencyUs"]["maximum"] < 120_000_000
        and oracle["result"]["stable"]
        and native["result"]["acceptedLines"] == oracle["result"]["acceptedLines"]
        and native["result"]["rawDetectionBoxes"] == oracle["result"]["rawDetectionBoxes"]
        and native["result"]["suppressedDuplicateBoxes"]
        == oracle["result"]["suppressedDuplicateBoxes"]
    )
    report = {
        "schema": "light-ocr-tiled-core-report/1.2",
        "passed": passed,
        "contractVersion": "tiled-v1",
        "fixtureId": fixture["id"],
        "fixtureSha256": sha256(arguments.fixture),
        "pixelSha256": fixture["pixelSha256"],
        "platformId": arguments.platform_id,
        "runner": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "processor": platform.processor(),
            "python": platform.python_version(),
            "logicalCpus": psutil.cpu_count(),
            "totalMemoryBytes": psutil.virtual_memory().total,
        },
        "build": {
            "metadataSha256": sha256(arguments.build_metadata),
            "source": build_metadata["source"],
            "host": build_metadata["host"],
            "toolchain": build_metadata["toolchain"],
            "locks": build_metadata["locks"],
        },
        "oracleLockSha256": sha256(Path(__file__).with_name("oracle.lock.json")),
        "native": native,
        "oracle": oracle,
        "gates": gates,
        "observations": {
            "coreToPythonWarmMedian": {
                "observedRatio": ratios["warmMedian"],
                "enforced": False,
            },
            "coreToPythonWarmP95": {
                "observedRatio": ratios["warmP95"],
                "enforced": False,
            },
            "inferenceOnlyMedian": {
                "observedRatio": ratios["inferenceOnlyMedian"],
                "enforced": False,
            }
        },
    }
    serialized = json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n"
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
