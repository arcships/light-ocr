from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from tests.python.webgpu_report_fixtures import (
    LIFECYCLE_RSS_MIB,
    WEBGPU_CHAIN,
    ocr_line,
    qualification_case,
    qualification_layout,
    synthetic_gate_inputs,
)
from tools.webgpu import qualify


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
        cpu = {"result": {"lines": [ocr_line()]}}
        close = {"result": {"lines": [ocr_line(confidence=0.93)]}}
        self.assertTrue(qualify.quality_matches(cpu, close)[0])

        wrong_text = {"result": {"lines": [ocr_line(text="HELLO 124")]}}
        self.assertFalse(qualify.quality_matches(cpu, wrong_text)[0])
        non_finite = {"result": {"lines": [ocr_line(confidence=float("nan"))]}}
        self.assertEqual(
            qualify.quality_matches(cpu, non_finite),
            (False, "confidence is not finite"),
        )

    def test_collect_evidence_passes_a_complete_synthetic_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sdk, native = qualification_layout(root)
            cases, profiles = synthetic_gate_inputs()
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

    def test_lifecycle_gate_uses_warmup_aware_baseline_when_rss_samples_present(self) -> None:
        # Real WebGPU/Dawn D3D12 lifecycle data: the first ~5 create/close
        # cycles fill the adapter/shader/pipeline cache and the RSS then
        # plateaus. The gate must skip those warmup cycles when computing the
        # retained-growth baseline, otherwise it reports +172 MiB (cache
        # warmup) as a leak and fails a healthy run.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sdk, native = qualification_layout(root)
            mib = 1024 * 1024
            # 20 cycles * 2 samples each, mirroring the observed WebGPU run:
            # ramp 270 -> 466 in first 5 cycles, then plateaus around 380-460.
            rss_bytes = [value * mib for value in LIFECYCLE_RSS_MIB]
            lifecycle_case = qualification_case(
                "allow", WEBGPU_CHAIN, lifecycle=True
            )
            lifecycle_case["cycles"] = 20
            lifecycle_case["lifecycle"]["rssBytes"] = rss_bytes
            lifecycle_case["lifecycle"]["retainedGrowthBytes"] = (
                rss_bytes[-1] - rss_bytes[0]
            )
            evidence = qualify.collect_evidence(
                platform_id="linux-x64",
                sdk=sdk,
                native=native,
                cases={"generated-hello-123:lifecycle": lifecycle_case},
                profiles={},
                graphics={
                    "source": "linux-drm-sysfs",
                    "adapters": [{"driver": "test", "driverVersion": "1.0"}],
                },
                rebuilt_from_source=True,
                required_fixtures=("generated-hello-123",),
            )
            lifecycle_gate = next(
                gate for gate in evidence["gates"] if gate["name"] == "repeated-lifecycle"
            )
            # The raw retainedGrowthBytes is +162 MiB (cache warmup), but the
            # warmup-aware growth measured from cycle 6 onwards is small.
            self.assertTrue(lifecycle_gate["passed"], lifecycle_gate)
            self.assertIn("warmupAwareGrowth=", lifecycle_gate["detail"])
            # Sanity: raw growth exceeds the ceiling, warmup-aware does not.
            self.assertGreater(
                lifecycle_case["lifecycle"]["retainedGrowthBytes"],
                qualify.MAX_RETAINED_GROWTH_BYTES,
            )
            warmup_aware = (
                rss_bytes[-1] - rss_bytes[2 * qualify.LIFECYCLE_WARMUP_CYCLES]
            )
            self.assertLessEqual(
                abs(warmup_aware), qualify.MAX_RETAINED_GROWTH_BYTES
            )


if __name__ == "__main__":
    unittest.main()
