from __future__ import annotations

import copy
import unittest

from tools import tiled_release_gate


def entry(identity: str, median: int = 100, p95: int = 120, peak: int = 200) -> dict:
    return {
        "key": {"identity": identity},
        "medianUs": median,
        "p95Us": p95,
        "absolutePeakBytes": peak,
    }


class TiledReleaseGateTests(unittest.TestCase):
    @staticmethod
    def core_report(ratio: float, enforced: bool = False) -> dict:
        runtime = {
            "normalizedConfigSchemaVersion": "1.2",
            "coreVersion": "0.2.0",
        }
        return {
            "schema": "light-ocr-tiled-core-report/1.2",
            "passed": True,
            "platformId": "macos-arm64",
            "fixtureId": "fixture",
            "fixtureSha256": "fixture-sha",
            "pixelSha256": "pixel-sha",
            "contractVersion": "tiled-v1",
            "runner": {"system": "Darwin", "machine": "arm64"},
            "build": {"host": {"system": "Darwin"}},
            "native": {
                "modelBundleId": tiled_release_gate.BUNDLE_ID,
                "warmup": 5,
                "iterations": 10,
                "latencyUs": {"maximum": 1_000_000},
                "runtime": runtime,
            },
            "oracle": {
                "modelBundleId": tiled_release_gate.BUNDLE_ID,
                "warmup": 5,
                "iterations": 10,
                "latencyUs": {"maximum": 1_000_000},
            },
            "observations": {
                "coreToPythonWarmMedian": {
                    "observedRatio": ratio,
                    "enforced": enforced,
                },
                "coreToPythonWarmP95": {
                    "observedRatio": ratio,
                    "enforced": enforced,
                },
                "inferenceOnlyMedian": {
                    "observedRatio": ratio,
                    "enforced": enforced,
                }
            },
        }

    def test_core_python_ratios_are_non_blocking_observations(self) -> None:
        report = self.core_report(1.50)
        tiled_release_gate.validate_core(
            report,
            "macos-arm64",
            "fixture",
            {"fixtureSha256": "fixture-sha", "pixelSha256": "pixel-sha"},
        )

    def test_core_python_observations_cannot_silently_become_gates(self) -> None:
        report = self.core_report(1.0, enforced=True)
        with self.assertRaisesRegex(RuntimeError, "cross-runtime observation"):
            tiled_release_gate.validate_core(
                report,
                "macos-arm64",
                "fixture",
                {"fixtureSha256": "fixture-sha", "pixelSha256": "pixel-sha"},
            )

    def test_node_bootstrap_gate_accepts_the_documented_limits(self) -> None:
        node = {"latencyUs": {"median": 110, "p95": 138}}
        core = {"latencyUs": {"median": 100, "p95": 120}}

        tiled_release_gate.gate_node_against_core(
            node, core, 600 * 1024 * 1024, 540 * 1024 * 1024, "fixture"
        )

    def test_node_bootstrap_gate_rejects_latency_overhead(self) -> None:
        node = {"latencyUs": {"median": 111, "p95": 120}}
        core = {"latencyUs": {"median": 100, "p95": 120}}

        with self.assertRaisesRegex(RuntimeError, "latency gate"):
            tiled_release_gate.gate_node_against_core(
                node, core, 600 * 1024 * 1024, 540 * 1024 * 1024, "fixture"
            )

    def test_node_bootstrap_gate_rejects_peak_overhead(self) -> None:
        node = {"latencyUs": {"median": 100, "p95": 120}}
        core = {"latencyUs": {"median": 100, "p95": 120}}

        with self.assertRaisesRegex(RuntimeError, "peak gate"):
            tiled_release_gate.gate_node_against_core(
                node,
                core,
                605 * 1024 * 1024,
                540 * 1024 * 1024,
                "fixture",
            )

    def test_accepts_an_identical_reviewed_baseline(self) -> None:
        entries = [entry(str(index)) for index in range(36)]
        candidate = {"entries": copy.deepcopy(entries)}
        accepted = {"status": "accepted", "entries": copy.deepcopy(entries)}

        tiled_release_gate.compare_accepted(candidate, accepted)

    def test_rejects_a_real_fifteen_percent_regression(self) -> None:
        entries = [entry(str(index)) for index in range(36)]
        candidate = {"entries": copy.deepcopy(entries)}
        accepted = {"status": "accepted", "entries": copy.deepcopy(entries)}
        candidate["entries"][7]["p95Us"] = 139

        with self.assertRaisesRegex(RuntimeError, "exceeds 15%"):
            tiled_release_gate.compare_accepted(candidate, accepted)

    def test_rejects_a_changed_runner_identity(self) -> None:
        entries = [entry(str(index)) for index in range(36)]
        candidate = {"entries": copy.deepcopy(entries)}
        accepted = {"status": "accepted", "entries": copy.deepcopy(entries)}
        candidate["entries"][0]["key"]["runner"] = "new-runner"

        with self.assertRaisesRegex(RuntimeError, "requalification"):
            tiled_release_gate.compare_accepted(candidate, accepted)


if __name__ == "__main__":
    unittest.main()
