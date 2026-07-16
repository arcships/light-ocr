from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools.apple import (
    accept_qualification,
    collect_qualification,
    fallback_gate,
    package_bundle,
    performance_gate,
)


class AppleQualificationCollectorTests(unittest.TestCase):
    @staticmethod
    def write_json(path: Path, value: dict[str, object]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding="utf-8")

    @staticmethod
    def hashed(value: dict[str, object]) -> dict[str, object]:
        result = dict(value)
        result["reportSha256"] = collect_qualification.report_hash(result)
        return result

    @staticmethod
    def apple_execution() -> dict[str, object]:
        return {
            "requestedProvider": "apple",
            "detection": {
                "modelSha256": "det",
                "qualificationId": "apple-test",
                "deviceValidated": True,
                "sessionFallback": False,
            },
            "recognition": {
                "modelSha256": "rec",
                "qualificationId": "apple-test",
                "deviceValidated": True,
                "sessionFallback": False,
            },
        }

    def test_collects_a_locally_qualified_device(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            acceptance = {
                "qualificationId": "apple-test",
                "models": {
                    "artifactId": "artifact",
                    "detectionPackageSha256": "det",
                    "recognitionPackageSha256": "rec",
                    "detectionCpuSha256": "cpu-det",
                    "recognitionCpuSha256": "cpu-rec",
                },
                "routing": {
                    "recognitionWidthMultiple": 32,
                    "recognitionAneMaximumWidth": 1600,
                    "recognitionRuntimeWidthBuckets": [320, 3200],
                    "maximumCachedFunctions": 2,
                },
                "performance": {
                    "workloadIds": [
                        "generated-hello-123", "paddleocr-xfund-form"
                    ],
                    "coldStartWorkloadId": "generated-hello-123",
                    "maximumResidentGrowthAfter100PagesBytes": 64,
                },
                "compatibility": {
                    "minimumQualifiedDevices": 1,
                },
            }
            acceptance_path = root / "acceptance.json"
            self.write_json(acceptance_path, acceptance)
            acceptance_hash = collect_qualification.hashlib.sha256(
                acceptance_path.read_bytes()
            ).hexdigest()
            for identifier, family in (("m4", "Apple M4"),):
                directory = root / "reports" / identifier
                self.write_json(directory / "identity.json", {
                    "expectedDeviceFamily": family,
                    "deviceBrand": family + " Pro",
                    "operatingSystem": "macOS",
                    "runnerLabel": "runner",
                })
                self.write_json(directory / "model-qualification.json", self.hashed({
                    "qualificationId": "apple-test",
                    "gate": {"passed": True, "coverageComplete": True},
                    "routing": {
                        "recognitionWidthMultiple": 32,
                        "aneMaximumWidth": 1600,
                        "runtimeWidthBuckets": [320, 3200],
                        "maximumCachedFunctions": 2,
                    },
                    "models": {
                        "artifactId": "artifact",
                        "detection": {"packageSha256": "det"},
                        "recognition": {"packageSha256": "rec"},
                    },
                }))
                self.write_json(directory / "quality.json", self.hashed({
                    "qualificationId": "apple-test",
                    "acceptanceSha256": acceptance_hash,
                    "models": {
                        "detectionPackageSha256": "det",
                        "recognitionPackageSha256": "rec",
                        "qualificationId": "apple-test",
                    },
                    "passed": True,
                }))
                self.write_json(directory / "performance.json", self.hashed({
                    "qualificationId": "apple-test",
                    "acceptanceSha256": acceptance_hash,
                    "passed": True,
                    "coldStartWorkloadId": "generated-hello-123",
                    "workloads": [
                        {"fixtureId": "generated-hello-123", "appleRuns": [{
                            "execution": self.apple_execution()
                        }]},
                        {"fixtureId": "paddleocr-xfund-form", "appleRuns": [{
                            "execution": self.apple_execution()
                        }]},
                    ],
                }))
                self.write_json(directory / "cache-concurrency.json", self.hashed({
                    "passed": True,
                    "processes": 4,
                    "records": [{"execution": self.apple_execution()}],
                }))
                self.write_json(directory / "lifecycle.json", self.hashed({
                    "passed": True,
                    "lifecycleMode": "pages",
                    "measuredCycles": 100,
                    "residentBytes": {"growth": 32},
                    "execution": self.apple_execution(),
                }))
            output = root / "candidate.json"
            with mock.patch("sys.argv", [
                "collect_qualification.py",
                "--reports-root", str(root / "reports"),
                "--acceptance", str(acceptance_path),
                "--git-commit", "abc123",
                "--output", str(output),
            ]):
                self.assertEqual(collect_qualification.main(), 0)
            candidate = json.loads(output.read_text("utf-8"))
            self.assertEqual(candidate["status"], "candidate")
            self.assertEqual(candidate["qualifiedDeviceFamilies"], ["Apple M4"])
            self.assertEqual(candidate["modelPackageSha256"], {
                "detection": "det", "recognition": "rec"
            })

    def test_rejects_tampered_hashed_report(self) -> None:
        report = self.hashed({"qualificationId": "apple-test", "passed": True})
        report["passed"] = False
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "report.json"
            self.write_json(path, report)
            with self.assertRaisesRegex(RuntimeError, "report hash mismatch"):
                collect_qualification.validate_hashed_report(path, "apple-test")

    def test_rejects_execution_from_a_different_model(self) -> None:
        execution = self.apple_execution()
        execution["recognition"]["modelSha256"] = "different"
        with self.assertRaisesRegex(RuntimeError, "not bound to the locked model"):
            collect_qualification.validate_execution_models(
                [{"execution": execution}], "apple-test", ("det", "rec"), "test"
            )

    def test_rejects_unvalidated_execution_as_reviewed_evidence(self) -> None:
        execution = self.apple_execution()
        execution["recognition"]["deviceValidated"] = False
        with self.assertRaisesRegex(RuntimeError, "validated device"):
            collect_qualification.validate_execution_models(
                [{"execution": execution}], "apple-test", ("det", "rec"), "test"
            )

    def test_validates_the_locked_unqualified_device_fallback(self) -> None:
        execution = {"requestedProvider": "apple"}
        for stage, model_hash in (("detection", "cpu-det"),
                                  ("recognition", "cpu-rec")):
            execution[stage] = {
                "requestedProvider": "apple",
                "actualProviderChain": ["CPUExecutionProvider"],
                "device": "cpu",
                "precision": "fp32",
                "modelSha256": model_hash,
                "sessionFallback": True,
                "fallbackReason": "apple_device_unqualified",
            }
        fallback_gate.validate_cpu_fallback(
            {"execution": execution}, ("cpu-det", "cpu-rec")
        )
        execution["recognition"]["sessionFallback"] = False
        with self.assertRaisesRegex(RuntimeError, "locked CPU fallback path"):
            fallback_gate.validate_cpu_fallback(
                {"execution": execution}, ("cpu-det", "cpu-rec")
            )

    def test_accepts_and_validates_a_reviewed_provider_baseline(self) -> None:
        acceptance = {
            "qualificationId": "apple-test",
            "models": {
                "artifactId": "artifact",
                "detectionPackageSha256": "d" * 64,
                "recognitionPackageSha256": "r" * 64,
                "detectionCpuSha256": "c" * 64,
                "recognitionCpuSha256": "e" * 64,
            },
            "compatibility": {
                "minimumQualifiedDevices": 1,
            },
        }
        acceptance_bytes = json.dumps(acceptance).encode("utf-8")
        acceptance_sha256 = collect_qualification.hashlib.sha256(
            acceptance_bytes
        ).hexdigest()
        candidate = {
            "schema": "light-ocr-apple-provider-baselines/1.0",
            "status": "candidate",
            "qualificationId": "apple-test",
            "generatedFromCommit": "a" * 40,
            "acceptanceSha256": acceptance_sha256,
            "modelArtifactId": "artifact",
            "modelPackageSha256": {
                "detection": "d" * 64,
                "recognition": "r" * 64,
            },
            "qualifiedDeviceFamilies": ["Apple M4"],
            "devices": [
                {"deviceFamily": "Apple M4"},
            ],
        }
        candidate["reportSha256"] = collect_qualification.report_hash(candidate)
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            candidate_path = root / "candidate.json"
            accepted_path = root / "accepted.json"
            self.write_json(candidate_path, candidate)
            with mock.patch("sys.argv", [
                "accept_qualification.py",
                "--candidate", str(candidate_path),
                "--approved-by-commit", "b" * 40,
                "--output", str(accepted_path),
            ]):
                self.assertEqual(accept_qualification.main(), 0)
            accepted = json.loads(accepted_path.read_text("utf-8"))
            self.assertEqual(accepted["status"], "accepted")
            self.assertEqual(
                package_bundle.accepted_device_families(
                    accepted_path, acceptance, acceptance_sha256
                ),
                ["Apple M4"],
            )

    def test_rejects_tampered_accepted_provider_baseline(self) -> None:
        report = {
            "schema": "light-ocr-apple-provider-baselines/1.0",
            "status": "accepted",
            "reportSha256": "0" * 64,
        }
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "accepted.json"
            self.write_json(path, report)
            with self.assertRaisesRegex(RuntimeError, "not an accepted intact"):
                package_bundle.accepted_device_families(path, {}, "unused")


class ApplePerformanceGateTests(unittest.TestCase):
    @staticmethod
    def sample_run(
        cache_status: str, resident: int, lifetime_peak: int
    ) -> dict[str, object]:
        return {
            "execution": {
                "detection": {"modelCacheStatus": cache_status},
                "recognition": {"modelCacheStatus": cache_status},
            },
            "memoryBytes": {
                "residentMaximum": resident,
                "peakResident": lifetime_peak,
            },
        }

    def test_warm_rss_uses_only_cache_hit_measurement_window(self) -> None:
        runs = [
            self.sample_run("compiled_cache_miss", 1_000, 2_000),
            self.sample_run("compiled_cache_hit", 700, 2_100),
            self.sample_run("compiled_cache_hit", 750, 2_200),
        ]
        self.assertEqual(performance_gate.warm_peak_resident_bytes(runs), 750)

    def test_warm_rss_requires_a_cache_hit_run(self) -> None:
        runs = [self.sample_run("compiled_cache_miss", 1_000, 2_000)]
        self.assertIsNone(performance_gate.warm_peak_resident_bytes(runs))


if __name__ == "__main__":
    unittest.main()
