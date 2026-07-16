#!/usr/bin/env python3
"""Run the locked CPU/Apple latency, CPU-time, cold-start, and RSS gate."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import statistics
import subprocess


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ACCEPTANCE = ROOT / "tools" / "apple" / "acceptance.json"
DEFAULT_REPORT = ROOT / "reports" / "apple" / "performance.json"
DEFAULT_FIXTURES = ROOT / "corpus" / "fixtures"


def directory_bytes(path: Path) -> int:
    return sum(item.stat().st_size for item in path.rglob("*") if item.is_file())


def fixture_arguments(fixtures: Path, fixture_id: str) -> list[str]:
    directory = fixtures / fixture_id
    fixture = json.loads((directory / "fixture.json").read_text("utf-8"))
    return [
        "--pixels", str(directory / "pixels.bin"),
        "--width", str(fixture["width"]),
        "--height", str(fixture["height"]),
        "--stride", str(fixture["stride"]),
        "--format", fixture["pixelFormat"],
    ]


def run_benchmark(
    executable: Path, bundle: Path, fixtures: Path, fixture_id: str,
    profile: str, warmup: int, iterations: int, output: Path,
) -> dict[str, object]:
    process = subprocess.run(
        [
            str(executable),
            "--bundle", str(bundle),
            *fixture_arguments(fixtures, fixture_id),
            "--profile", profile,
            "--warmup", str(warmup),
            "--iterations", str(iterations),
            "--report", str(output),
        ],
        check=False,
        capture_output=True,
        text=True,
        timeout=900,
    )
    if process.returncode != 0:
        raise RuntimeError(
            f"benchmark failed for {fixture_id}/{profile}: "
            f"{process.stdout[-4000:]}{process.stderr[-4000:]}"
        )
    return json.loads(process.stdout)


def median(values: list[float]) -> float:
    return float(statistics.median(values))


def compiled_cache_hit(run: dict[str, object]) -> bool:
    execution = run["execution"]
    return all(
        str(execution[stage]["modelCacheStatus"]).endswith("cache_hit")
        for stage in ("detection", "recognition")
    )


def warm_peak_resident_bytes(runs: list[dict[str, object]]) -> int | None:
    measurements = [
        int(run["memoryBytes"]["residentMaximum"])
        for run in runs
        if compiled_cache_hit(run)
    ]
    return max(measurements) if measurements else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-benchmark", type=Path, required=True)
    parser.add_argument("--cpu-bundle", type=Path, required=True)
    parser.add_argument("--apple-bundle", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, default=DEFAULT_FIXTURES)
    parser.add_argument("--acceptance", type=Path, default=DEFAULT_ACCEPTANCE)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--workload", action="append", dest="workloads")
    parser.add_argument("--clear-compiled-cache", action="store_true")
    arguments = parser.parse_args()
    acceptance = json.loads(arguments.acceptance.read_text("utf-8"))
    thresholds = acceptance["performance"]
    locked_workloads = list(thresholds["workloadIds"])
    cold_start_workload = str(thresholds["coldStartWorkloadId"])
    if cold_start_workload not in locked_workloads:
        parser.error("coldStartWorkloadId must name a locked workload")
    workloads = arguments.workloads or locked_workloads
    if workloads != locked_workloads:
        parser.error("--workload must exactly match the locked acceptance order")
    report_root = arguments.report.resolve().parent / "performance-runs"
    report_root.mkdir(parents=True, exist_ok=True)
    cache_root = (
        Path.home() / "Library" / "Caches" / "com.arcships.light-ocr" / "coreml-v1"
    )
    records: list[dict[str, object]] = []
    qualifying_workloads = 0
    failures: list[str] = []
    maximum_peak_resident = 0
    for fixture_id in workloads:
        cpu_runs: list[dict[str, object]] = []
        apple_runs: list[dict[str, object]] = []
        for run in range(int(thresholds["coldStartRuns"])):
            cpu_runs.append(run_benchmark(
                arguments.native_benchmark.resolve(), arguments.cpu_bundle.resolve(),
                arguments.fixtures.resolve(), fixture_id, "cpu_fast", 5,
                int(thresholds["warmRuns"]),
                report_root / f"{fixture_id}-cpu-{run}.json",
            ))
            if run == 0 and arguments.clear_compiled_cache:
                shutil.rmtree(cache_root, ignore_errors=True)
            apple_runs.append(run_benchmark(
                arguments.native_benchmark.resolve(), arguments.apple_bundle.resolve(),
                arguments.fixtures.resolve(), fixture_id, "apple_interactive", 5,
                int(thresholds["warmRuns"]),
                report_root / f"{fixture_id}-apple-{run}.json",
            ))

        cpu_p50 = median([float(run["latencyUs"]["median"]) for run in cpu_runs])
        apple_p50 = median([
            float(run["latencyUs"]["median"]) for run in apple_runs
        ])
        speedup = cpu_p50 / apple_p50
        cpu_time = median([
            float(run["processCpuUs"]) / float(run["iterations"])
            for run in cpu_runs
        ])
        apple_cpu_time = median([
            float(run["processCpuUs"]) / float(run["iterations"])
            for run in apple_runs
        ])
        cpu_time_reduction = 1.0 - apple_cpu_time / cpu_time
        cold_starts = [
            (
                float(run["loadUs"]) + float(run["engineInitializationUs"])
                + float(run["firstPredictionUs"])
            ) / 1000.0
            for run in apple_runs
        ]
        cache_statuses = [
            {
                "detection": run["execution"]["detection"]["modelCacheStatus"],
                "recognition": run["execution"]["recognition"]["modelCacheStatus"],
            }
            for run in apple_runs
        ]
        cache_hit_indices = [
            index for index, run in enumerate(apple_runs) if compiled_cache_hit(run)
        ]
        workload_peak = warm_peak_resident_bytes(apple_runs)
        process_lifetime_peak = max(
            int(run["memoryBytes"]["peakResident"]) for run in apple_runs
        )
        maximum_peak_resident = max(maximum_peak_resident, workload_peak or 0)
        workload_failures: list[str] = []
        if speedup < thresholds["minimumCpuP50Speedup"]:
            workload_failures.append("P50 speedup is below the locked threshold")
        else:
            qualifying_workloads += 1
        if cpu_time_reduction < thresholds["minimumCpuTimeReduction"]:
            workload_failures.append("CPU-time reduction is below the locked threshold")
        if not cache_hit_indices:
            workload_failures.append("no compiled-cache-hit run measured warm RSS")
        elif workload_peak > thresholds["maximumWarmPeakResidentBytes"]:
            workload_failures.append("peak resident memory exceeds the locked ceiling")
        if fixture_id == cold_start_workload:
            for index, cold_start in enumerate(cold_starts):
                cache_hit = all(
                    value.endswith("cache_hit")
                    for value in cache_statuses[index].values()
                )
                ceiling = (
                    thresholds["maximumCompiledCacheHitColdStartMilliseconds"]
                    if cache_hit
                    else thresholds["maximumCompiledCacheMissColdStartMilliseconds"]
                )
                if cold_start > ceiling:
                    workload_failures.append(
                        f"cold start {index} exceeds its cache-aware ceiling"
                    )
        failures.extend(f"{fixture_id}: {value}" for value in workload_failures)
        records.append({
            "fixtureId": fixture_id,
            "passed": not workload_failures,
            "failures": workload_failures,
            "cpuP50Microseconds": cpu_p50,
            "appleP50Microseconds": apple_p50,
            "speedup": speedup,
            "cpuTimePerIterationMicroseconds": cpu_time,
            "appleCpuTimePerIterationMicroseconds": apple_cpu_time,
            "cpuTimeReduction": cpu_time_reduction,
            "appleColdStartMilliseconds": cold_starts,
            "coldStartGateApplied": fixture_id == cold_start_workload,
            "appleCacheStatuses": cache_statuses,
            "appleWarmPeakResidentBytes": workload_peak,
            "appleProcessLifetimePeakResidentBytes": process_lifetime_peak,
            "cpuRuns": cpu_runs,
            "appleRuns": apple_runs,
        })

    bundle_increment = (
        directory_bytes(arguments.apple_bundle.resolve())
        - directory_bytes(arguments.cpu_bundle.resolve())
    )
    if qualifying_workloads < thresholds["minimumTargetWorkloads"]:
        failures.append("fewer than two workloads passed the Provider Gate speedup")
    if bundle_increment > thresholds["maximumAppleBundleIncrementBytes"]:
        failures.append("Apple model bundle increment exceeds the locked ceiling")
    report: dict[str, object] = {
        "schemaVersion": "1.0",
        "qualificationId": acceptance["qualificationId"],
        "coldStartWorkloadId": cold_start_workload,
        "device": platform.platform(),
        "acceptanceSha256": hashlib.sha256(arguments.acceptance.read_bytes()).hexdigest(),
        "passed": not failures,
        "failures": failures,
        "qualifyingWorkloads": qualifying_workloads,
        "bundleIncrementBytes": bundle_increment,
        "maximumAppleWarmPeakResidentBytes": maximum_peak_resident,
        "workloads": records,
    }
    encoded = json.dumps(report, ensure_ascii=False, sort_keys=True,
                         separators=(",", ":"))
    report["reportSha256"] = hashlib.sha256(encoded.encode("utf-8")).hexdigest()
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({
        "passed": report["passed"],
        "failures": failures,
        "qualifyingWorkloads": qualifying_workloads,
        "report": str(arguments.report),
    }, ensure_ascii=False, sort_keys=True))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
