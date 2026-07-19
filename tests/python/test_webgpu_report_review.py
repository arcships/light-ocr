from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
import unittest

from tools.webgpu import build_runtime, qualify, review_reports


def pending_lock() -> dict[str, object]:
    lock = build_runtime.load_lock()
    qualification = lock["qualification"]
    qualification["status"] = "development-pending-device-validation"
    qualification["providerGatePassed"] = False
    qualification["productionArtifactQualified"] = False
    qualification["qualifiedArtifactSetSha256"] = {
        "linux-x64": None,
        "windows-x64": None,
    }
    qualification["qualificationReportSha256"] = {
        "linux-x64": None,
        "windows-x64": None,
    }
    return lock


def line() -> dict[str, object]:
    return {
        "text": "HELLO 123",
        "confidence": 0.95,
        "box": [[1.0, 2.0], [20.0, 2.0], [20.0, 12.0], [1.0, 12.0]],
    }


def node_case(mode: str, chain: list[str]) -> dict[str, object]:
    cpu = mode == "cpu"
    return {
        "schemaVersion": "1.1",
        "ok": True,
        "result": {
            "lines": [line()],
            "deterministic": True,
            "sha256": "1" * 64,
        },
        "engine": {
            "executionProvider": (
                "CPUExecutionProvider" if cpu else "WebGpuExecutionProvider"
            ),
            "execution": {
                "sessions": {
                    "detection": {
                        "actualProviderChain": chain,
                        "precision": "fp32",
                    },
                    "recognition": {
                        "actualProviderChain": chain,
                        "precision": "fp32",
                    },
                }
            },
        },
        "latencyUs": {
            "minimum": 80 if not cpu else 120,
            "p50": 100 if not cpu else 160,
            "p95": 120 if not cpu else 160,
            "maximum": 140 if not cpu else 180,
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
        "processCpuUs": 1000,
        "measuredWallUs": 2000,
        "averageProcessCpuCores": 0.5,
        "lifecycle": {
            "residentMinimumBytes": 100 * 1024 * 1024,
            "residentMaximumBytes": 110 * 1024 * 1024,
            "retainedGrowthBytes": 1024,
        },
    }


def cases_and_profiles() -> tuple[dict[str, dict], dict[str, dict]]:
    cases: dict[str, dict] = {}
    profiles: dict[str, dict] = {}
    for fixture in qualify.DEFAULT_FIXTURES:
        cases[f"{fixture}:cpu"] = node_case("cpu", ["CPUExecutionProvider"])
        cases[f"{fixture}:allow"] = node_case(
            "allow", ["WebGpuExecutionProvider", "CPUExecutionProvider"]
        )
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
        profiles[f"{fixture}:allow"] = {
            "files": [f"{fixture}-allow.json"],
            "fileSha256": {f"{fixture}-allow.json": "2" * 64},
            "nodeCounts": {"WebGpuExecutionProvider": 10},
            "operators": {},
        }
    canary = qualify.DEFAULT_FIXTURES[0]
    auto = node_case("auto", ["WebGpuExecutionProvider", "CPUExecutionProvider"])
    auto["host"] = {"platform": "test", "architecture": "x64"}
    auto["engine"]["execution"]["selectionTrace"] = {
        "orderedCandidates": ["webgpu", "cpu"],
        "selectedProvider": "webgpu",
    }
    cases[f"{canary}:auto"] = auto
    lifecycle = node_case("allow", ["WebGpuExecutionProvider", "CPUExecutionProvider"])
    lifecycle["warmup"] = 0
    lifecycle["iterations"] = 1
    lifecycle["cycles"] = 20
    cases[f"{canary}:lifecycle"] = lifecycle
    cases["native-cpp:auto"] = {
        "ok": True,
        "engineInitializationUs": 1000,
        "firstPredictionUs": 2000,
        "warmup": 1,
        "iterations": 10,
        "memoryBytes": {"peakResident": 120 * 1024 * 1024},
        "execution": {
            "requestedProvider": "auto",
            "selectionTrace": {
                "orderedCandidates": ["webgpu", "cpu"],
                "selectedProvider": "webgpu",
            },
            "detection": {
                "actualProviderChain": [
                    "WebGpuExecutionProvider",
                    "CPUExecutionProvider",
                ]
            },
            "recognition": {
                "actualProviderChain": [
                    "WebGpuExecutionProvider",
                    "CPUExecutionProvider",
                ]
            },
        },
    }
    for key in (f"{canary}:auto", f"{canary}:lifecycle", "native-cpp:auto"):
        profiles[key] = {
            "files": [key.replace(":", "-") + ".json"],
            "fileSha256": {key.replace(":", "-") + ".json": "4" * 64},
            "nodeCounts": {"WebGpuExecutionProvider": 10},
            "operators": {},
        }
    return cases, profiles


def sdk_manifest(lock: dict[str, object], platform_id: str) -> dict[str, object]:
    records = []
    for spec in build_runtime.artifact_plan(lock, platform_id):
        records.append(
            {
                "path": spec["outputPath"],
                "bytes": 17,
                "sha256": hashlib.sha256(spec["outputPath"].encode()).hexdigest(),
                "role": spec["role"],
                "sourcePackage": spec["package"],
                "sourcePath": spec["sourcePath"],
            }
        )
    platform = lock["platforms"][platform_id]
    platform_identity = {
        "id": platform_id,
        "operatingSystem": platform["operatingSystem"],
        "architecture": platform["architecture"],
    }
    if "libc" in platform:
        platform_identity["libc"] = platform["libc"]
    runtime_paths = [record["outputPath"] for record in platform["runtimeFiles"]]
    provider_path = next(
        record["outputPath"]
        for record in platform["runtimeFiles"]
        if record["role"] == "webgpu-plugin"
    )
    package_records = []
    for name in ("onnxruntime", "webgpu"):
        package = lock["packages"][name]
        package_records.append(
            {
                "name": name,
                **{
                    key: package[key]
                    for key in (
                        "id",
                        "version",
                        "source",
                        "catalog",
                        "bytes",
                        "sha512",
                    )
                },
            }
        )
    return {
        "schemaVersion": 2,
        "contractId": lock["contractId"],
        "platform": platform_identity,
        "runtime": build_runtime.runtime_identity(lock, platform_id),
        "artifacts": {
            "linkLibrary": platform["linkLibrary"]["outputPath"],
            "providerLibrary": provider_path,
            "runtimeFiles": runtime_paths,
            "files": sorted(records, key=lambda value: value["path"]),
            "artifactSetSha256": build_runtime.artifact_set_digest(records),
        },
        "headers": {
            "directory": "include",
            "onnxruntimeVersion": "1.24.4",
            "files": [record for record in records if record["role"] == "header"],
        },
        "packages": package_records,
        "sessionOptions": copy.deepcopy(lock["sessionOptions"]),
        "qualification": copy.deepcopy(lock["qualification"]),
    }


def runtime_descriptor(
    manifest: dict[str, object], platform_id: str
) -> dict[str, object]:
    records = {record["path"]: record for record in manifest["artifacts"]["files"]}
    runtime_records = [
        {
            "path": f"native/{PurePosixPath(relative).name}",
            "bytes": records[relative]["bytes"],
            "sha256": records[relative]["sha256"],
        }
        for relative in manifest["artifacts"]["runtimeFiles"]
    ]
    addon = {"path": "native/light_ocr_node.node", "bytes": 23, "sha256": "5" * 64}
    platform = {
        "id": platform_id,
        "os": "linux" if platform_id == "linux-x64" else "win32",
        "architecture": "x86_64",
    }
    if platform_id == "linux-x64":
        platform["libc"] = "glibc"
    return {
        "schemaVersion": "2.0",
        "platform": platform,
        "runtime": {
            "flavor": "webgpu",
            "kind": "onnxruntime-plugin-webgpu",
            "version": "1.24.4",
            "abi": "onnxruntime-c-api-24-plugin-ep-0.1",
            "artifacts": runtime_records,
        },
        "qualificationOnly": True,
        "released": False,
        "autoPolicy": {
            "id": f"{platform_id}-v1",
            "version": 1,
            "providers": ["webgpu", "cpu"],
        },
        "providers": {
            "webgpu": {
                "runtimeProvider": "WebGpuExecutionProvider",
                "providerVersion": "0.1.0",
                "qualificationId": manifest["qualification"]["evidenceId"],
                "providerLibrary": runtime_records[1],
                "artifacts": runtime_records[1:],
            },
            "cpu": {
                "runtimeProvider": "CPUExecutionProvider",
                "qualificationId": "cpu-baseline-v1",
                "artifacts": [runtime_records[0]],
            },
        },
        "addon": addon,
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
    manifest = sdk_manifest(lock, platform_id)
    manifest_path = artifacts / "sdk-artifact-manifest.json"
    write_json(manifest_path, manifest)
    descriptor = runtime_descriptor(manifest, platform_id)
    descriptor_path = artifacts / "native-runtime-descriptor.json"
    write_json(descriptor_path, descriptor)

    sdk = directory / "sdk"
    native = directory / "native-package" / "native"
    sdk.mkdir()
    native.mkdir(parents=True)
    write_json(sdk / "artifact-manifest.json", manifest)
    write_json(native / "runtime-descriptor.json", descriptor)
    payload_bytes = sum(
        record["bytes"]
        for record in [descriptor["addon"], *descriptor["runtime"]["artifacts"]]
    )
    with (native / "payload.bin").open("wb") as stream:
        stream.truncate(payload_bytes)
    cases, profiles = cases_and_profiles()
    graphics = {
        "source": "synthetic",
        "adapters": [{"driver": "test", "driverVersion": "1.0"}],
    }
    report = qualify.collect_evidence(
        platform_id=platform_id,
        sdk=sdk,
        native=directory / "native-package",
        cases=cases,
        profiles=profiles,
        graphics=graphics,
        rebuilt_from_source=True,
    )
    report_path = directory / "qualification-report.json"
    write_json(report_path, report)
    (directory / "qualification-report.sha256").write_text(
        f"{review_reports.sha256(report_path)}  qualification-report.json\n",
        "utf-8",
    )


class WebGpuReportReviewTest(unittest.TestCase):
    def setUp(self) -> None:
        self.revision = review_reports.current_revision()

    def create_pair(self, root: Path) -> Path:
        lock = pending_lock()
        lock_path = root / "runtime-lock.json"
        write_json(lock_path, lock)
        for platform_id in review_reports.PLATFORMS:
            write_platform_report(root, platform_id, lock)
        return lock_path

    def test_collects_intact_pair_as_manual_review_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock_path = self.create_pair(root)
            candidate = review_reports.collect_pair(
                root, expected_revision=self.revision, lock_path=lock_path
            )
            self.assertTrue(candidate["mechanicalValidationPassed"])
            self.assertEqual(candidate["status"], "manual-review-required")
            self.assertEqual(set(candidate["platforms"]), set(review_reports.PLATFORMS))
            self.assertEqual(
                candidate["reportSha256"], review_reports.canonical_hash(candidate)
            )

    def test_rejects_report_changed_without_sidecar_update(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock_path = self.create_pair(root)
            report_path = root / "linux-x64" / "qualification-report.json"
            report = json.loads(report_path.read_text("utf-8"))
            report["passed"] = False
            write_json(report_path, report)
            with self.assertRaisesRegex(RuntimeError, "report hash mismatch"):
                review_reports.collect_pair(
                    root, expected_revision=self.revision, lock_path=lock_path
                )

    def test_rejects_rehashed_report_with_missing_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock_path = self.create_pair(root)
            report_path = root / "linux-x64" / "qualification-report.json"
            report = json.loads(report_path.read_text("utf-8"))
            report["gates"].pop()
            write_json(report_path, report)
            (report_path.parent / "qualification-report.sha256").write_text(
                f"{review_reports.sha256(report_path)}  qualification-report.json\n",
                "utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "gate inventory"):
                review_reports.collect_pair(
                    root, expected_revision=self.revision, lock_path=lock_path
                )

    def test_rejects_cross_revision_report_pair(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock_path = self.create_pair(root)
            with self.assertRaisesRegex(RuntimeError, "report identity"):
                review_reports.collect_pair(
                    root, expected_revision="a" * 40, lock_path=lock_path
                )

    def test_collects_staggered_platform_revisions_without_override(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock_path = self.create_pair(root)
            report_path = root / "windows-x64" / "qualification-report.json"
            report = json.loads(report_path.read_text("utf-8"))
            report["sourceRevision"] = "b" * 40
            write_json(report_path, report)
            (report_path.parent / "qualification-report.sha256").write_text(
                f"{review_reports.sha256(report_path)}  qualification-report.json\n",
                "utf-8",
            )
            candidate = review_reports.collect_pair(root, lock_path=lock_path)
            self.assertEqual(
                candidate["sourceRevisions"],
                {"linux-x64": self.revision, "windows-x64": "b" * 40},
            )

    def test_rejects_tampered_copied_descriptor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock_path = self.create_pair(root)
            descriptor_path = (
                root / "windows-x64" / "artifacts" / "native-runtime-descriptor.json"
            )
            descriptor = json.loads(descriptor_path.read_text("utf-8"))
            descriptor["released"] = True
            write_json(descriptor_path, descriptor)
            with self.assertRaisesRegex(RuntimeError, "descriptor policy"):
                review_reports.collect_pair(
                    root, expected_revision=self.revision, lock_path=lock_path
                )

    def test_production_lock_must_bind_the_reviewed_pair(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.create_pair(root)
            lock = pending_lock()
            qualification = lock["qualification"]
            qualification["status"] = "production-qualified"
            qualification["providerGatePassed"] = True
            qualification["productionArtifactQualified"] = True
            qualification["qualifiedArtifactSetSha256"] = {}
            qualification["qualificationReportSha256"] = {}
            for platform_id in review_reports.PLATFORMS:
                report = json.loads(
                    (root / platform_id / "qualification-report.json").read_text(
                        "utf-8"
                    )
                )
                qualification["qualifiedArtifactSetSha256"][platform_id] = report[
                    "sdk"
                ]["artifactSetSha256"]
                qualification["qualificationReportSha256"][platform_id] = (
                    root / platform_id / "qualification-report.sha256"
                ).read_text("utf-8").split()[0]
            production_lock = root / "production-runtime-lock.json"
            write_json(production_lock, lock)
            candidate = review_reports.collect_pair(root, lock_path=production_lock)
            self.assertEqual(candidate["status"], "production-qualified")

            qualification["qualificationReportSha256"]["windows-x64"] = "0" * 64
            write_json(production_lock, lock)
            with self.assertRaisesRegex(RuntimeError, "differs from the reviewed"):
                review_reports.collect_pair(root, lock_path=production_lock)


if __name__ == "__main__":
    unittest.main()
