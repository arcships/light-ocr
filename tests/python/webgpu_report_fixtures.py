from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
from unittest import mock

from tests.python.npm_release_fixtures import release_metadata
from tests.python.webgpu_runtime_fixtures import create_fake_packages, pending_lock
from tools import npm_release
from tools.webgpu import build_runtime, qualify, review_reports


WEBGPU_CHAIN = ["WebGpuExecutionProvider", "CPUExecutionProvider"]
LIFECYCLE_RSS_MIB = (
    270, 288, 322, 352, 374, 391, 406, 413, 429, 406,
    414, 410, 423, 419, 442, 419, 450, 445, 448, 427,
    455, 451, 464, 460, 458, 432, 440, 428, 435, 422,
    430, 425, 428, 432, 430, 428, 429, 431, 430, 432,
)


def ocr_line(
    *, text: str = "HELLO 123", confidence: float = 0.95
) -> dict[str, object]:
    return {
        "text": text,
        "confidence": confidence,
        "box": [[1.0, 2.0], [20.0, 2.0], [20.0, 12.0], [1.0, 12.0]],
    }


def _node_case(
    mode: str,
    chain: list[str],
    *,
    precision: str,
    retained_growth: int,
) -> dict[str, object]:
    cpu = mode == "cpu"
    return {
        "schemaVersion": "1.1",
        "ok": True,
        "result": {
            "lines": [ocr_line()],
            "deterministic": True,
            "sha256": "1" * 64,
        },
        "engine": {
            "executionProvider": (
                "CPUExecutionProvider" if cpu else "WebGpuExecutionProvider"
            ),
            "execution": {
                "sessions": {
                    stage: {"actualProviderChain": chain, "precision": precision}
                    for stage in ("detection", "recognition")
                }
            },
        },
        "latencyUs": {
            "minimum": 120 if cpu else 80,
            "p50": 160 if cpu else 100,
            "p95": 160 if cpu else 120,
            "maximum": 180 if cpu else 140,
        },
        "warmup": 2,
        "iterations": 10,
        "cycles": 3,
        "engineInitializationUs": {
            "minimum": 1000,
            "p50": 1000,
            "maximum": 1000,
            "values": [1000, 1000, 1000],
        },
        "firstPredictionUs": 2000,
        "firstPredictionUsByCycle": [2000, 2000, 2000],
        "lifecycle": {
            "residentMinimumBytes": 100 * 1024 * 1024,
            "residentMaximumBytes": 110 * 1024 * 1024,
            "retainedGrowthBytes": retained_growth,
        },
    }


def qualification_case(
    mode: str, chain: list[str], *, lifecycle: bool = False
) -> dict[str, object]:
    return _node_case(
        mode,
        chain,
        precision="fp16" if mode == "allow" else "fp32",
        retained_growth=1024 if lifecycle else 0,
    )


def review_node_case(mode: str, chain: list[str]) -> dict[str, object]:
    value = _node_case(mode, chain, precision="fp32", retained_growth=1024)
    value.update(
        {"processCpuUs": 1000, "measuredWallUs": 2000, "averageProcessCpuCores": 0.5}
    )
    return value


def qualification_layout(root: Path) -> tuple[Path, Path]:
    sdk = root / "sdk"
    native = root / "native-package"
    sdk.mkdir()
    (native / "native").mkdir(parents=True)
    write_json(
        sdk / "artifact-manifest.json",
        {
            "contractId": "native-webgpu-plugin-0.1.0-ort-1.24.4-v1",
            "artifacts": {"artifactSetSha256": "2" * 64},
            "runtime": {"kind": "onnxruntime-plugin-webgpu"},
            "qualification": {"evidenceId": "synthetic-evidence"},
        },
    )
    write_json(
        native / "native" / "runtime-descriptor.json",
        {"runtime": {"kind": "onnxruntime-plugin-webgpu"}},
    )
    return sdk, native


def synthetic_gate_inputs() -> tuple[dict[str, dict], dict[str, dict]]:
    cpu = qualification_case("cpu", ["CPUExecutionProvider"])
    allow = qualification_case("allow", WEBGPU_CHAIN, lifecycle=True)
    auto = qualification_case("auto", WEBGPU_CHAIN)
    auto["host"] = {"platform": "linux", "architecture": "x64"}
    auto["engine"]["execution"]["selectionTrace"] = {
        "orderedCandidates": ["webgpu", "cpu"],
        "selectedProvider": "webgpu",
    }
    cases = {
        "generated-hello-123:cpu": cpu,
        "generated-hello-123:fp32": qualification_case("fp32", WEBGPU_CHAIN),
        "generated-hello-123:allow": allow,
        "generated-hello-123:strict": {
            "schemaVersion": "1.1",
            "ok": True,
            "expectedRejection": True,
            "error": {
                "code": "unsupported_capability",
                "message": "The WebGPU model requires a bounded CPU operator partition",
                "detail": "required operators: Concat, Gather, Slice",
            },
        },
        "generated-hello-123:auto": auto,
        "generated-hello-123:lifecycle": qualification_case(
            "allow", WEBGPU_CHAIN, lifecycle=True
        ),
        "native-cpp:auto": native_auto_case(),
    }
    cases["generated-hello-123:lifecycle"]["cycles"] = 20
    profiles = {
        key: {
            "files": [f"{label}.json"],
            "nodeCounts": {"WebGpuExecutionProvider": 10},
        }
        for key, label in (
            ("generated-hello-123:fp32", "fp32"),
            ("generated-hello-123:allow", "allow"),
            ("generated-hello-123:auto", "auto"),
            ("native-cpp:auto", "native-cpp-auto"),
            ("generated-hello-123:lifecycle", "lifecycle"),
        )
    }
    return cases, profiles


def native_auto_case() -> dict[str, object]:
    sessions = {
        stage: {"actualProviderChain": WEBGPU_CHAIN}
        for stage in ("detection", "recognition")
    }
    return {
        "ok": True,
        "engineInitializationUs": 1000,
        "firstPredictionUs": 2000,
        "memoryBytes": {"peakResident": 120 * 1024 * 1024},
        "execution": {
            "requestedProvider": "auto",
            "selectionTrace": {
                "orderedCandidates": ["webgpu", "cpu"],
                "selectedProvider": "webgpu",
            },
            **sessions,
        },
        "warmup": 1,
        "iterations": 10,
    }


def review_cases_and_profiles() -> tuple[dict[str, dict], dict[str, dict]]:
    cases: dict[str, dict] = {}
    profiles: dict[str, dict] = {}
    for fixture in qualify.DEFAULT_FIXTURES:
        cases[f"{fixture}:cpu"] = review_node_case("cpu", ["CPUExecutionProvider"])
        cases[f"{fixture}:allow"] = review_node_case("allow", WEBGPU_CHAIN)
        cases[f"{fixture}:strict"] = {
            "schemaVersion": "1.1",
            "ok": True,
            "expectedRejection": True,
            "error": {
                "code": "unsupported_capability",
                "message": "The WebGPU model requires a bounded CPU operator partition",
                "detail": "required operators: Concat, Gather, Slice",
            },
        }
        profiles[f"{fixture}:allow"] = profile(f"{fixture}-allow", "2")
    canary = qualify.DEFAULT_FIXTURES[0]
    auto = review_node_case("auto", WEBGPU_CHAIN)
    auto["host"] = {"platform": "test", "architecture": "x64"}
    auto["engine"]["execution"]["selectionTrace"] = {
        "orderedCandidates": ["webgpu", "cpu"],
        "selectedProvider": "webgpu",
    }
    cases[f"{canary}:auto"] = auto
    lifecycle = review_node_case("allow", WEBGPU_CHAIN)
    lifecycle.update({"warmup": 0, "iterations": 1, "cycles": 20})
    cases[f"{canary}:lifecycle"] = lifecycle
    cases["native-cpp:auto"] = native_auto_case()
    for key in (f"{canary}:auto", f"{canary}:lifecycle", "native-cpp:auto"):
        profiles[key] = profile(key.replace(":", "-"), "4")
    return cases, profiles


def profile(name: str, digest_character: str) -> dict[str, object]:
    filename = f"{name}.json"
    return {
        "files": [filename],
        "fileSha256": {filename: digest_character * 64},
        "nodeCounts": {"WebGpuExecutionProvider": 10},
        "operators": {},
    }


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", "utf-8")


def write_platform_report(
    root: Path, platform_id: str, lock: dict[str, object]
) -> None:
    directory = root / platform_id
    artifacts = directory / "artifacts"
    artifacts.mkdir(parents=True)
    packages = create_fake_packages(directory, lock)
    sdk = directory / "sdk"
    manifest_path = build_runtime.stage_runtime(lock, platform_id, packages, sdk)
    manifest = build_runtime.validate_sdk(sdk, lock)
    (artifacts / "sdk-artifact-manifest.json").write_bytes(manifest_path.read_bytes())

    build_dir = directory / "build"
    binaries = build_dir / "bin"
    binaries.mkdir(parents=True)
    (binaries / "light_ocr_node.node").write_bytes(b"addon")
    native_package = directory / "native-package"
    arguments = argparse.Namespace(
        platform_id=platform_id,
        build_dir=build_dir,
        metadata_dir=release_metadata(directory),
        output_dir=native_package,
        runtime_flavor="webgpu",
        webgpu_artifact_manifest=manifest_path,
        qualification_build=True,
    )
    with mock.patch(
        "tools.npm_release.webgpu_runtime.load_lock", return_value=lock
    ), redirect_stdout(io.StringIO()):
        npm_release.stage_native(arguments)
    descriptor_path = native_package / "native" / "runtime-descriptor.json"
    (artifacts / "native-runtime-descriptor.json").write_bytes(
        descriptor_path.read_bytes()
    )

    cases, profiles = review_cases_and_profiles()
    report = qualify.collect_evidence(
        platform_id=platform_id,
        sdk=sdk,
        native=native_package,
        cases=cases,
        profiles=profiles,
        graphics={
            "source": "synthetic",
            "adapters": [{"driver": "test", "driverVersion": "1.0"}],
        },
        rebuilt_from_source=True,
    )
    report_path = directory / "qualification-report.json"
    write_json(report_path, report)
    (directory / "qualification-report.sha256").write_text(
        f"{review_reports.sha256(report_path)}  qualification-report.json\n",
        "utf-8",
    )


def create_report_pair(root: Path) -> Path:
    lock = pending_lock()
    lock_path = root / "runtime-lock.json"
    write_json(lock_path, lock)
    for platform_id in review_reports.PLATFORMS:
        write_platform_report(root, platform_id, lock)
    return lock_path
