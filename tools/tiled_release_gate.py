#!/usr/bin/env python3
"""Validate, aggregate, and approve tiled-v1 four-platform release evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
BUNDLE_ID = "ppocrv6-small-onnx-20260714.2"
CONTRACT_VERSION = "tiled-v1"
CORE_ABSOLUTE_LIMIT = 1024 * 1024 * 1024
NODE_ABSOLUTE_LIMIT = CORE_ABSOLUTE_LIMIT + 64 * 1024 * 1024
PLATFORMS = {
    "linux-x64": {"os": "linux", "arch": "x64"},
    "windows-x64": {"os": "win32", "arch": "x64"},
    "macos-arm64": {"os": "darwin", "arch": "arm64"},
    "macos-x64": {"os": "darwin", "arch": "x64"},
}
FIXTURES = (
    "tiled-small-text-2048",
    "tiled-dense-2048",
    "tiled-four-way-intersection-2048",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path) -> Any:
    if not path.is_file():
        raise RuntimeError(f"required tiled release report is missing: {path}")
    return json.loads(path.read_text("utf-8"))


def write(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def finite_positive(value: Any, name: str) -> float:
    if not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise RuntimeError(f"{name} must be a finite positive number")
    return float(value)


def fixture_records() -> dict[str, dict[str, Any]]:
    lock_path = ROOT / "corpus" / "tiled-v1" / "ground-truth.lock.json"
    lock = load(lock_path)
    records = {record["fixtureId"]: record for record in lock["fixtures"]}
    if any(fixture not in records for fixture in FIXTURES):
        raise RuntimeError("ground-truth lock does not contain the release fixture subset")
    for fixture_id in FIXTURES:
        path = ROOT / "corpus" / "tiled-v1" / "fixtures" / fixture_id / "fixture.json"
        if sha256(path) != records[fixture_id]["fixtureSha256"]:
            raise RuntimeError(f"fixture lock mismatch: {fixture_id}")
    return records


def validate_memory(
    report: dict[str, Any], platform_id: str, fixture_id: str, mode: str
) -> int:
    if (
        report.get("schemaVersion") != "1.0"
        or not report.get("passed")
        or report.get("profile") != "tiled_v1"
        or report.get("diagnosticsMode") != mode
        or report.get("modelBundleId") != BUNDLE_ID
        or report.get("runtime", {}).get("detectionStrategy") != "tiled"
        or report.get("result", {}).get("acceptedLines", 0) <= 0
    ):
        raise RuntimeError(
            f"invalid Core memory report for {platform_id}/{fixture_id}/{mode}"
        )
    peak = int(finite_positive(report["memoryBytes"]["peakResident"], "Core peak"))
    if peak > CORE_ABSOLUTE_LIMIT:
        raise RuntimeError(f"Core peak exceeds 1 GiB: {platform_id}/{fixture_id}/{mode}")
    if mode == "on":
        passes = report["result"]["detectionPasses"]
        if (
            len(passes) != 4
            or report["result"]["maxLiveDetectionPassBuffers"] != 1
            or any(item["tensorShape"][2] > 1280 or item["tensorShape"][3] > 1280 for item in passes)
        ):
            raise RuntimeError(f"invalid tiled diagnostics: {platform_id}/{fixture_id}")
    return peak


def validate_lifecycle(report: dict[str, Any], platform_id: str) -> None:
    resident = report.get("residentBytes", {})
    gate = report.get("gate", {})
    if (
        report.get("schemaVersion") != "1.0"
        or not report.get("ok")
        or not report.get("passed")
        or report.get("profile") != "tiled_v1"
        or report.get("warmupCycles") != 5
        or report.get("measuredCycles") != 10
        or gate.get("maximumGrowthBytes") != 32 * 1024 * 1024
        or gate.get("maximumGrowthPerCycleBytes") != 8 * 1024 * 1024
        or resident.get("growth", 0) > gate.get("maximumGrowthBytes", 0)
        or resident.get("growthPerCycle", 0)
        > gate.get("maximumGrowthPerCycleBytes", 0)
    ):
        raise RuntimeError(f"invalid tiled lifecycle report: {platform_id}")


def validate_core(
    report: dict[str, Any], platform_id: str, fixture_id: str, fixture: dict[str, Any]
) -> None:
    if (
        report.get("schema") != "light-ocr-tiled-core-report/1.2"
        or not report.get("passed")
        or report.get("platformId") != platform_id
        or report.get("fixtureId") != fixture_id
        or report.get("fixtureSha256") != fixture["fixtureSha256"]
        or report.get("pixelSha256") != fixture["pixelSha256"]
        or report.get("contractVersion") != CONTRACT_VERSION
    ):
        raise RuntimeError(f"invalid Core benchmark report: {platform_id}/{fixture_id}")
    native = report["native"]
    oracle = report["oracle"]
    expected = PLATFORMS[platform_id]
    system_names = {"linux": "Linux", "win32": "Windows", "darwin": "Darwin"}
    machine = report["runner"]["machine"].lower()
    machine_matches = (
        expected["arch"] == "arm64" and machine in {"arm64", "aarch64"}
    ) or (
        expected["arch"] == "x64" and machine in {"x86_64", "amd64", "x64"}
    )
    if (
        native.get("modelBundleId") != BUNDLE_ID
        or oracle.get("modelBundleId") != BUNDLE_ID
        or native.get("warmup") != 5
        or native.get("iterations") != 10
        or oracle.get("warmup") != 5
        or oracle.get("iterations") != 10
        or native["latencyUs"]["maximum"] >= 120_000_000
        or oracle["latencyUs"]["maximum"] >= 120_000_000
        or native.get("runtime", {}).get("normalizedConfigSchemaVersion") != "1.2"
        or native.get("runtime", {}).get("coreVersion") != "0.2.0"
        or report["runner"]["system"] != system_names[expected["os"]]
        or not machine_matches
        or report.get("build", {}).get("host", {}).get("system")
        != system_names[expected["os"]]
    ):
        raise RuntimeError(f"invalid Core benchmark identity: {platform_id}/{fixture_id}")
    observations = report.get("observations", {})
    for key in (
        "coreToPythonWarmMedian",
        "coreToPythonWarmP95",
        "inferenceOnlyMedian",
    ):
        observation = observations.get(key, {})
        if observation.get("enforced") is not False:
            raise RuntimeError(
                f"invalid cross-runtime observation: {platform_id}/{fixture_id}/{key}"
            )
        finite_positive(observation.get("observedRatio"), key)


def validate_node(
    report: dict[str, Any], platform_id: str, fixture_id: str,
    fixture: dict[str, Any], node_major: int, mode: str,
) -> int:
    expected_platform = PLATFORMS[platform_id]
    runtime = report.get("runtime", {})
    engine_info = runtime.get("engineInfo", {})
    if (
        report.get("schema") != "light-ocr-tiled-node-report/1.0"
        or not report.get("passed")
        or report.get("platformId") != platform_id
        or report.get("fixtureId") != fixture_id
        or report.get("fixtureSha256") != fixture["fixtureSha256"]
        or report.get("pixelSha256") != fixture["pixelSha256"]
        or report.get("contractVersion") != CONTRACT_VERSION
        or report.get("diagnosticsMode") != mode
        or not runtime.get("node", "").startswith(f"v{node_major}.")
        or runtime.get("platform") != expected_platform["os"]
        or runtime.get("arch") != expected_platform["arch"]
        or runtime.get("intraOpThreads") != 1
        or runtime.get("interOpThreads") != 1
        or not isinstance(runtime.get("packageVersion"), str)
        or runtime.get("packageVersion") != engine_info.get("coreVersion")
        or engine_info.get("modelBundleId") != BUNDLE_ID
        or engine_info.get("normalizedConfigSchemaVersion") != "1.2"
        or engine_info.get("detectionStrategy") != "tiled"
        or engine_info.get("tiledDetection", {}).get("contractVersion")
        != CONTRACT_VERSION
        or report.get("warmup") != 5
        or report.get("iterations") != 10
        or report["latencyUs"]["maximum"] >= 120_000_000
    ):
        raise RuntimeError(
            f"invalid Node report: {platform_id}/{fixture_id}/node{node_major}/{mode}"
        )
    return int(finite_positive(report["memoryBytes"]["peakResident"], "Node peak"))


def observe_node_against_core(
    node: dict[str, Any], native: dict[str, Any], node_peak: int, core_peak: int,
    identity: str,
) -> tuple[float, float, int]:
    median_ratio = node["latencyUs"]["median"] / native["latencyUs"]["median"]
    p95_ratio = node["latencyUs"]["p95"] / native["latencyUs"]["p95"]
    if node_peak > NODE_ABSOLUTE_LIMIT:
        raise RuntimeError(f"Node absolute peak gate failed: {identity}")
    return median_ratio, p95_ratio, node_peak - core_peak


def collect(reports_root: Path, git_commit: str) -> dict[str, Any]:
    fixtures = fixture_records()
    entries: list[dict[str, Any]] = []
    for platform_id in PLATFORMS:
        lifecycle_path = reports_root / platform_id / "core-lifecycle.json"
        validate_lifecycle(load(lifecycle_path), platform_id)
        for fixture_id in FIXTURES:
            fixture = fixtures[fixture_id]
            core_path = reports_root / platform_id / "core" / f"{fixture_id}.json"
            core = load(core_path)
            validate_core(core, platform_id, fixture_id, fixture)
            if core["build"]["source"]["gitRevision"] != git_commit:
                raise RuntimeError(
                    f"Core report commit mismatch: {platform_id}/{fixture_id}"
                )
            memory_paths = {
                mode: reports_root / platform_id / "core-memory" / f"{fixture_id}-{mode}.json"
                for mode in ("on", "off")
            }
            core_peaks = {
                mode: validate_memory(load(path), platform_id, fixture_id, mode)
                for mode, path in memory_paths.items()
            }
            native = core["native"]
            oracle = core["oracle"]
            entries.append({
                "key": {
                    "contractVersion": CONTRACT_VERSION,
                    "bundleId": BUNDLE_ID,
                    "fixtureId": fixture_id,
                    "fixtureHash": fixture["fixtureSha256"],
                    "pixelHash": fixture["pixelSha256"],
                    "platformId": platform_id,
                    "runnerClass": {
                        **core["runner"],
                        "toolchain": core["build"]["toolchain"],
                    },
                    "implementation": "core",
                    "coreVersion": native["runtime"]["coreVersion"],
                    "nodeVersion": None,
                    "threadConfig": "ort-intra-1/inter-1",
                },
                "samples": 10,
                "medianUs": native["latencyUs"]["median"],
                "p95Us": native["latencyUs"]["p95"],
                "inferenceOnlyMedianUs": native["inferenceOnlyUs"]["median"],
                "absolutePeakBytes": max(core_peaks.values()),
                "diagnosticsPeakBytes": core_peaks,
                "comparator": {
                    "identity": "python-tiled-v1-oracle",
                    "medianUs": oracle["latencyUs"]["median"],
                    "p95Us": oracle["latencyUs"]["p95"],
                    "inferenceOnlyMedianUs": oracle["inferenceOnlyUs"]["median"],
                },
                "resultHashes": [
                    native["result"]["stableSha256"],
                    oracle["result"]["stableSha256"],
                ],
                "reportDigests": [
                    sha256(core_path),
                    core["build"]["metadataSha256"],
                    sha256(lifecycle_path),
                    *[sha256(path) for path in memory_paths.values()],
                ],
            })
            for node_major in (22, 24):
                node_paths = {
                    mode: reports_root / platform_id / f"node{node_major}" / f"{fixture_id}-{mode}.json"
                    for mode in ("on", "off")
                }
                node_reports = {mode: load(path) for mode, path in node_paths.items()}
                node_peaks = {
                    mode: validate_node(
                        report, platform_id, fixture_id, fixture, node_major, mode
                    )
                    for mode, report in node_reports.items()
                }
                node = node_reports["on"]
                node_peak = max(node_peaks.values())
                core_peak = max(core_peaks.values())
                median_ratio, p95_ratio, peak_overhead = observe_node_against_core(
                    node, native, node_peak, core_peak,
                    f"{platform_id}/{fixture_id}/node{node_major}",
                )
                entries.append({
                    "key": {
                        "contractVersion": CONTRACT_VERSION,
                        "bundleId": BUNDLE_ID,
                        "fixtureId": fixture_id,
                        "fixtureHash": fixture["fixtureSha256"],
                        "pixelHash": fixture["pixelSha256"],
                        "platformId": platform_id,
                        "runnerClass": {
                            "system": node["runtime"]["platform"],
                            "release": node["runtime"]["osRelease"],
                            "machine": node["runtime"]["arch"],
                            "cpu": node["runtime"]["cpu"],
                            "logicalCpus": node["runtime"]["logicalCpus"],
                            "totalMemoryBytes": node["runtime"]["totalMemoryBytes"],
                            "napi": node["runtime"]["napi"],
                        },
                        "implementation": "node",
                        "coreVersion": node["runtime"]["engineInfo"]["coreVersion"],
                        "packageVersion": node["runtime"]["packageVersion"],
                        "nodeVersion": node["runtime"]["node"],
                        "threadConfig": "ort-intra-1/inter-1",
                    },
                    "samples": 10,
                    "medianUs": node["latencyUs"]["median"],
                    "p95Us": node["latencyUs"]["p95"],
                    "inferenceOnlyMedianUs": node["inferenceOnlyUs"]["median"],
                    "absolutePeakBytes": node_peak,
                    "diagnosticsPeakBytes": node_peaks,
                    "comparator": {
                        "identity": "same-runner-core",
                        "medianUs": native["latencyUs"]["median"],
                        "p95Us": native["latencyUs"]["p95"],
                        "absolutePeakBytes": core_peak,
                        "observedMedianRatio": median_ratio,
                        "observedP95Ratio": p95_ratio,
                        "observedPeakOverheadBytes": peak_overhead,
                        "enforced": False,
                        "reason": "separate process baselines and non-interleaved samples",
                    },
                    "resultHashes": [node["result"]["stableSha256"]],
                    "reportDigests": [sha256(path) for path in node_paths.values()],
                })
    expected_entries = len(PLATFORMS) * len(FIXTURES) * 3
    if len(entries) != expected_entries:
        raise RuntimeError("incomplete tiled release evidence matrix")
    keys = [json.dumps(entry["key"], sort_keys=True) for entry in entries]
    if len(keys) != len(set(keys)):
        raise RuntimeError("duplicate tiled baseline key")
    return {
        "schema": "light-ocr-tiled-platform-baselines/1.0",
        "status": "candidate",
        "contractVersion": CONTRACT_VERSION,
        "modelBundleId": BUNDLE_ID,
        "generatedFromCommit": git_commit,
        "groundTruthLockSha256": sha256(
            ROOT / "corpus" / "tiled-v1" / "ground-truth.lock.json"
        ),
        "matrix": {
            "platforms": list(PLATFORMS),
            "fixtures": list(FIXTURES),
            "implementations": ["core", "node22", "node24"],
            "entries": expected_entries,
        },
        "entries": entries,
    }


def compare_accepted(candidate: dict[str, Any], accepted: dict[str, Any]) -> None:
    if accepted.get("status") != "accepted":
        raise RuntimeError("committed tiled baseline is not accepted")
    accepted_entries = {
        json.dumps(entry["key"], sort_keys=True): entry for entry in accepted["entries"]
    }
    if len(accepted_entries) != len(candidate["entries"]):
        raise RuntimeError("accepted tiled baseline matrix is incomplete")
    for current in candidate["entries"]:
        key = json.dumps(current["key"], sort_keys=True)
        previous = accepted_entries.get(key)
        if previous is None:
            raise RuntimeError("runner or baseline identity changed; requalification is required")
        for metric in ("medianUs", "p95Us", "absolutePeakBytes"):
            if current[metric] > previous[metric] * 1.15:
                raise RuntimeError(f"tiled baseline regression exceeds 15%: {metric}")


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    collect_parser = subparsers.add_parser("collect")
    collect_parser.add_argument("--reports-root", type=Path, required=True)
    collect_parser.add_argument("--git-commit", required=True)
    collect_parser.add_argument("--output", type=Path, required=True)
    collect_parser.add_argument("--accepted", type=Path)
    approve_parser = subparsers.add_parser("approve")
    approve_parser.add_argument("--candidate", type=Path, required=True)
    approve_parser.add_argument("--approved-by-commit", required=True)
    approve_parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    if arguments.command == "collect":
        candidate = collect(arguments.reports_root, arguments.git_commit)
        if arguments.accepted:
            compare_accepted(candidate, load(arguments.accepted))
        write(arguments.output, candidate)
        print(json.dumps({"passed": True, "entries": len(candidate["entries"])}))
        return 0

    candidate = load(arguments.candidate)
    if (
        candidate.get("schema") != "light-ocr-tiled-platform-baselines/1.0"
        or candidate.get("status") != "candidate"
        or candidate.get("contractVersion") != CONTRACT_VERSION
        or candidate.get("modelBundleId") != BUNDLE_ID
        or len(candidate.get("entries", [])) != 36
        or candidate.get("matrix", {}).get("entries") != 36
    ):
        raise RuntimeError("invalid tiled baseline candidate")
    keys = [json.dumps(entry.get("key"), sort_keys=True) for entry in candidate["entries"]]
    if len(set(keys)) != 36:
        raise RuntimeError("invalid tiled baseline candidate keys")
    approved = dict(candidate)
    approved["status"] = "accepted"
    approved["approvedByCommit"] = arguments.approved_by_commit
    write(arguments.output, approved)
    print(json.dumps({"approved": True, "entries": len(approved["entries"])}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
