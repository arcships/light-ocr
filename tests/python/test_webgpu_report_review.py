from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tests.python.webgpu_report_fixtures import (
    create_report_pair,
    pending_lock,
    write_json,
)
from tools.webgpu import review_reports


class WebGpuReportReviewTest(unittest.TestCase):
    def setUp(self) -> None:
        self.revision = review_reports.current_revision()

    def test_collects_intact_pair_as_manual_review_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock_path = create_report_pair(root)
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
            lock_path = create_report_pair(root)
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
            lock_path = create_report_pair(root)
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
            lock_path = create_report_pair(root)
            with self.assertRaisesRegex(RuntimeError, "report identity"):
                review_reports.collect_pair(
                    root, expected_revision="a" * 40, lock_path=lock_path
                )

    def test_collects_staggered_platform_revisions_without_override(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock_path = create_report_pair(root)
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
            lock_path = create_report_pair(root)
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
            create_report_pair(root)
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
