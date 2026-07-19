from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "webgpu" / "qualify.py"
SPEC = importlib.util.spec_from_file_location("webgpu_qualify", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
qualify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(qualify)


def line(*, text: str = "HELLO 123", confidence: float = 0.95) -> dict[str, object]:
    return {
        "text": text,
        "confidence": confidence,
        "box": [[1.0, 2.0], [20.0, 2.0], [20.0, 12.0], [1.0, 12.0]],
    }


def report(
    mode: str, chain: list[str], *, lifecycle: bool = False
) -> dict[str, object]:
    cpu = mode == "cpu"
    value: dict[str, object] = {
        "schemaVersion": "1.1",
        "ok": True,
        "result": {
            "lines": [line()],
            "deterministic": True,
            "sha256": "1" * 64,
        },
        "engine": {
            "executionProvider": (
                "CPUExecutionProvider" if mode == "cpu" else "WebGpuExecutionProvider"
            ),
            "execution": {
                "sessions": {
                    "detection": {
                        "actualProviderChain": chain,
                        "precision": "fp16" if mode == "allow" else "fp32",
                    },
                    "recognition": {
                        "actualProviderChain": chain,
                        "precision": "fp16" if mode == "allow" else "fp32",
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
        "lifecycle": {
            "residentMinimumBytes": 100 * 1024 * 1024,
            "residentMaximumBytes": 110 * 1024 * 1024,
            "retainedGrowthBytes": 1024 if lifecycle else 0,
        },
    }
    return value


class WebGpuQualificationTest(unittest.TestCase):
    def test_qualification_rejects_source_changes(self) -> None:
        completed = subprocess.CompletedProcess(
            args=["git", "status"], returncode=0, stdout=" M src/core/engine.cpp\n"
        )
        with mock.patch.object(qualify.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(
                qualify.QualificationError, "clean source tree"
            ):
                qualify.require_clean_source()

    def test_explicit_node_headers_support_offline_build_setup(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            include = root / "include"
            include.mkdir()
            (include / "node_api.h").write_text("/* test */\n", "utf-8")
            with mock.patch.object(qualify.platform, "system", return_value="Linux"):
                actual_include, actual_library = qualify.node_development_files(
                    root / "work",
                    root / "logs",
                    offline=True,
                    include_override=include,
                    library_override=None,
                )
            self.assertEqual(actual_include, include.resolve())
            self.assertIsNone(actual_library)

    def test_profile_summary_records_provider_placement_and_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "canary-detection-0_2026.json").write_text(
                json.dumps(
                    [
                        {
                            "cat": "Node",
                            "name": "Conv_kernel_time",
                            "args": {"provider": "WebGpuExecutionProvider"},
                        },
                        {"cat": "Session", "name": "ignored", "args": {}},
                    ]
                ),
                "utf-8",
            )
            summary = qualify.profile_summary(root, "canary")
            self.assertEqual(summary["nodeCounts"], {"WebGpuExecutionProvider": 1})
            self.assertEqual(
                summary["operators"],
                {"WebGpuExecutionProvider": {"Conv_kernel_time": 1}},
            )
            self.assertRegex(
                summary["fileSha256"]["canary-detection-0_2026.json"],
                r"^[0-9a-f]{64}$",
            )

    def test_quality_gate_accepts_tolerance_and_rejects_invalid_results(self) -> None:
        cpu = {"result": {"lines": [line()]}}
        close = {"result": {"lines": [line(confidence=0.93)]}}
        self.assertTrue(qualify.quality_matches(cpu, close)[0])

        wrong_text = {"result": {"lines": [line(text="HELLO 124")]}}
        self.assertFalse(qualify.quality_matches(cpu, wrong_text)[0])
        non_finite = {"result": {"lines": [line(confidence=float("nan"))]}}
        self.assertEqual(
            qualify.quality_matches(cpu, non_finite),
            (False, "confidence is not finite"),
        )

    def test_collect_evidence_passes_a_complete_synthetic_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sdk = root / "sdk"
            native = root / "native-package"
            (native / "native").mkdir(parents=True)
            sdk.mkdir()
            (sdk / "artifact-manifest.json").write_text(
                json.dumps(
                    {
                        "contractId": "native-webgpu-plugin-0.1.0-ort-1.24.4-v1",
                        "artifacts": {"artifactSetSha256": "2" * 64},
                        "qualification": {"evidenceId": "synthetic-evidence"},
                    }
                ),
                "utf-8",
            )
            (native / "native" / "runtime-descriptor.json").write_text(
                json.dumps({"runtime": {"kind": "onnxruntime-plugin-webgpu"}}),
                "utf-8",
            )
            cpu = report("cpu", ["CPUExecutionProvider"])
            allow = report(
                "allow",
                ["WebGpuExecutionProvider", "CPUExecutionProvider"],
                lifecycle=True,
            )
            fp32 = report(
                "fp32", ["WebGpuExecutionProvider", "CPUExecutionProvider"]
            )
            strict = {
                "schemaVersion": "1.1",
                "ok": True,
                "expectedRejection": True,
                "error": {
                    "code": "unsupported_capability",
                    "message": "The WebGPU model requires a bounded CPU operator partition",
                    "detail": "required operators: Concat, Gather, Slice",
                },
            }
            auto = report("auto", ["WebGpuExecutionProvider", "CPUExecutionProvider"])
            auto["host"] = {"platform": "linux", "architecture": "x64"}
            auto["engine"]["execution"]["selectionTrace"] = {
                "orderedCandidates": ["webgpu", "cpu"],
                "selectedProvider": "webgpu",
            }
            cases = {
                "generated-hello-123:cpu": cpu,
                "generated-hello-123:fp32": fp32,
                "generated-hello-123:allow": allow,
                "generated-hello-123:strict": strict,
                "generated-hello-123:auto": auto,
                "generated-hello-123:lifecycle": report(
                    "allow",
                    ["WebGpuExecutionProvider", "CPUExecutionProvider"],
                    lifecycle=True,
                ),
                "native-cpp:auto": {
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
                },
            }
            cases["generated-hello-123:lifecycle"]["cycles"] = 20
            cases["native-cpp:auto"]["warmup"] = 1
            cases["native-cpp:auto"]["iterations"] = 10
            profiles = {
                "generated-hello-123:fp32": {
                    "files": ["fp32.json"],
                    "nodeCounts": {"WebGpuExecutionProvider": 10},
                },
                "generated-hello-123:allow": {
                    "files": ["allow.json"],
                    "nodeCounts": {"WebGpuExecutionProvider": 10},
                },
                "generated-hello-123:auto": {
                    "files": ["auto.json"],
                    "nodeCounts": {"WebGpuExecutionProvider": 10},
                },
                "native-cpp:auto": {
                    "files": ["native-cpp-auto.json"],
                    "nodeCounts": {"WebGpuExecutionProvider": 10},
                },
                "generated-hello-123:lifecycle": {
                    "files": ["lifecycle.json"],
                    "nodeCounts": {"WebGpuExecutionProvider": 10},
                },
            }
            evidence = qualify.collect_evidence(
                platform_id="linux-x64",
                sdk=sdk,
                native=native,
                cases=cases,
                profiles=profiles,
                graphics={
                    "source": "synthetic",
                    "adapters": [{"driver": "test", "driverVersion": "1.0"}],
                },
                rebuilt_from_source=True,
                required_fixtures=("generated-hello-123",),
            )
            self.assertTrue(evidence["passed"])
            self.assertTrue(all(gate["passed"] for gate in evidence["gates"]))

            reused_evidence = qualify.collect_evidence(
                platform_id="linux-x64",
                sdk=sdk,
                native=native,
                cases=cases,
                profiles=profiles,
                graphics={
                    "source": "synthetic",
                    "adapters": [{"driver": "test", "driverVersion": "1.0"}],
                },
                rebuilt_from_source=False,
                required_fixtures=("generated-hello-123",),
            )
            self.assertFalse(reused_evidence["passed"])
            self.assertEqual(
                next(
                    gate
                    for gate in reused_evidence["gates"]
                    if gate["name"] == "build-provenance"
                ),
                {
                    "name": "build-provenance",
                    "passed": False,
                    "detail": "--skip-build reused prior outputs; diagnostic evidence cannot qualify a release",
                },
            )


if __name__ == "__main__":
    unittest.main()
