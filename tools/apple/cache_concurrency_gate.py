#!/usr/bin/env python3
"""Verify Core ML compilation cache integrity under concurrent processes."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUNDLE = ROOT / "models" / "generated" / "ppocrv6-small-native-20260719.1"
DEFAULT_FIXTURE = ROOT / "corpus" / "fixtures" / "generated-hello-123"
DEFAULT_REPORT = ROOT / "reports" / "apple" / "cache-concurrency.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-benchmark", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, default=DEFAULT_BUNDLE)
    parser.add_argument("--fixture", type=Path, default=DEFAULT_FIXTURE)
    parser.add_argument("--processes", type=int, default=4)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    arguments = parser.parse_args()
    if arguments.processes < 2 or arguments.processes > 16:
        parser.error("--processes must be between 2 and 16")
    fixture = json.loads((arguments.fixture / "fixture.json").read_text("utf-8"))
    cache_root = (
        Path.home() / "Library" / "Caches" / "com.arcships.light-ocr" / "coreml-v1"
    )
    shutil.rmtree(cache_root, ignore_errors=True)
    commands: list[list[str]] = []
    for index in range(arguments.processes):
        commands.append([
            str(arguments.native_benchmark.resolve()),
            "--bundle", str(arguments.bundle.resolve()),
            "--pixels", str((arguments.fixture / "pixels.bin").resolve()),
            "--width", str(fixture["width"]),
            "--height", str(fixture["height"]),
            "--stride", str(fixture["stride"]),
            "--format", str(fixture["pixelFormat"]),
            "--profile", "apple_interactive",
            "--warmup", "0",
            "--iterations", "1",
            "--report", str(arguments.report.parent / f"cache-process-{index}.json"),
        ])
    processes = [
        subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        for command in commands
    ]
    records: list[dict[str, object]] = []
    failures: list[str] = []
    for index, process in enumerate(processes):
        try:
            stdout, stderr = process.communicate(timeout=180)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate()
            failures.append(f"process {index} timed out")
        if process.returncode != 0:
            failures.append(
                f"process {index} exited {process.returncode}: {(stdout + stderr)[-1000:]}"
            )
            continue
        try:
            records.append(json.loads(stdout))
        except json.JSONDecodeError as error:
            failures.append(f"process {index} returned invalid JSON: {error}")
    if len(records) == arguments.processes:
        result_hashes = {
            str(record["result"]["stableSha256"]) for record in records
        }
        if len(result_hashes) != 1:
            failures.append("concurrent processes produced different OCR results")
        for stage in ("detection", "recognition"):
            statuses = [
                str(record["execution"][stage]["modelCacheStatus"])
                for record in records
            ]
            if statuses.count("compiled_cache_miss") != 1:
                failures.append(f"{stage} did not produce exactly one cache miss")
            if statuses.count("compiled_cache_hit") != arguments.processes - 1:
                failures.append(f"{stage} cache hit count is invalid")
    temporary_paths = sorted(
        str(path.relative_to(cache_root))
        for path in cache_root.rglob("*.tmp.*")
    ) if cache_root.is_dir() else []
    if temporary_paths:
        failures.append("temporary cache directories remain after concurrent compilation")
    report: dict[str, object] = {
        "schemaVersion": "1.0",
        "passed": not failures,
        "failures": failures,
        "processes": arguments.processes,
        "records": records,
        "remainingTemporaryPaths": temporary_paths,
    }
    encoded = json.dumps(
        report, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    )
    report["reportSha256"] = hashlib.sha256(encoded.encode("utf-8")).hexdigest()
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({
        "passed": not failures,
        "failures": failures,
        "report": str(arguments.report),
    }, ensure_ascii=False, sort_keys=True))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
