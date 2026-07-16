#!/usr/bin/env python3
"""Validate and collect independent Apple device qualification reports."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ACCEPTANCE = ROOT / "tools" / "apple" / "acceptance.json"


def read_json(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise RuntimeError(f"required qualification report is missing: {path}")
    value = json.loads(path.read_text("utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError(f"qualification report is not an object: {path}")
    return value


def report_hash(report: dict[str, object]) -> str:
    value = dict(report)
    value.pop("reportSha256", None)
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    )
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def validate_hashed_report(
    path: Path, qualification_id: str
) -> dict[str, object]:
    report = read_json(path)
    if report.get("qualificationId") != qualification_id:
        raise RuntimeError(f"qualification ID mismatch: {path}")
    if report.get("reportSha256") != report_hash(report):
        raise RuntimeError(f"report hash mismatch: {path}")
    return report


def validate_execution_models(
    records: list[dict[str, object]], qualification_id: str,
    expected_hashes: tuple[str, str], context: str,
) -> None:
    if not records:
        raise RuntimeError(f"{context} contains no execution records")
    for record in records:
        execution = record.get("execution", {})
        if execution.get("requestedProvider") != "apple":
            raise RuntimeError(f"{context} did not request the Apple provider")
        for stage, expected_hash in zip(
            ("detection", "recognition"), expected_hashes, strict=True
        ):
            session = execution.get(stage, {})
            if (
                session.get("modelSha256") != expected_hash
                or session.get("qualificationId") != qualification_id
                or session.get("deviceValidated") is not True
                or session.get("sessionFallback") is not False
            ):
                raise RuntimeError(
                    f"{context} {stage} execution is not bound to the locked model and validated device"
                )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reports-root", type=Path, required=True)
    parser.add_argument("--acceptance", type=Path, default=DEFAULT_ACCEPTANCE)
    parser.add_argument("--git-commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    acceptance_path = arguments.acceptance.resolve()
    acceptance = read_json(acceptance_path)
    qualification_id = str(acceptance["qualificationId"])
    minimum_devices = int(acceptance["compatibility"]["minimumQualifiedDevices"])
    acceptance_sha256 = hashlib.sha256(acceptance_path.read_bytes()).hexdigest()
    expected_model_hashes = (
        str(acceptance["models"]["detectionPackageSha256"]),
        str(acceptance["models"]["recognitionPackageSha256"]),
    )

    devices: list[dict[str, object]] = []
    families: set[str] = set()
    model_artifact_id: str | None = None
    model_hashes: tuple[str, str] | None = None
    for directory in sorted(arguments.reports_root.resolve().iterdir()):
        if not directory.is_dir():
            continue
        identity = read_json(directory / "identity.json")
        family = str(identity.get("expectedDeviceFamily", ""))
        brand = str(identity.get("deviceBrand", ""))
        if not family or not brand.startswith(family):
            raise RuntimeError(f"device identity does not match {family}: {brand}")
        if family in families:
            raise RuntimeError(f"duplicate qualified device family: {family}")

        model = validate_hashed_report(
            directory / "model-qualification.json", qualification_id
        )
        quality = validate_hashed_report(directory / "quality.json", qualification_id)
        performance = validate_hashed_report(
            directory / "performance.json", qualification_id
        )
        cache = read_json(directory / "cache-concurrency.json")
        lifecycle = read_json(directory / "lifecycle.json")
        if not model.get("gate", {}).get("passed"):
            raise RuntimeError(f"model placement gate failed for {family}")
        if not model.get("gate", {}).get("coverageComplete"):
            raise RuntimeError(f"model shape coverage is incomplete for {family}")
        expected_routing = acceptance["routing"]
        observed_routing = model.get("routing", {})
        if (
            observed_routing.get("recognitionWidthMultiple")
            != expected_routing["recognitionWidthMultiple"]
            or observed_routing.get("aneMaximumWidth")
            != expected_routing["recognitionAneMaximumWidth"]
            or observed_routing.get("runtimeWidthBuckets")
            != expected_routing["recognitionRuntimeWidthBuckets"]
            or observed_routing.get("maximumCachedFunctions")
            != expected_routing["maximumCachedFunctions"]
        ):
            raise RuntimeError(f"model routing contract mismatch for {family}")
        if not quality.get("passed"):
            raise RuntimeError(f"quality gate failed for {family}")
        if quality.get("acceptanceSha256") != acceptance_sha256:
            raise RuntimeError(f"quality acceptance hash mismatch for {family}")
        if quality.get("models") != {
            "detectionPackageSha256": expected_model_hashes[0],
            "recognitionPackageSha256": expected_model_hashes[1],
            "qualificationId": qualification_id,
        }:
            raise RuntimeError(f"quality model identity mismatch for {family}")
        if not performance.get("passed"):
            raise RuntimeError(f"performance gate failed for {family}")
        if performance.get("acceptanceSha256") != acceptance_sha256:
            raise RuntimeError(f"performance acceptance hash mismatch for {family}")
        observed_workloads = [
            record.get("fixtureId") for record in performance.get("workloads", [])
        ]
        if observed_workloads != acceptance["performance"]["workloadIds"]:
            raise RuntimeError(f"performance workload contract mismatch for {family}")
        if (
            performance.get("coldStartWorkloadId")
            != acceptance["performance"]["coldStartWorkloadId"]
        ):
            raise RuntimeError(f"cold-start workload contract mismatch for {family}")
        if not cache.get("passed") or int(cache.get("processes", 0)) < 2:
            raise RuntimeError(f"cache concurrency gate failed for {family}")
        if cache.get("reportSha256") != report_hash(cache):
            raise RuntimeError(f"cache concurrency report hash mismatch for {family}")
        validate_execution_models(
            list(cache.get("records", [])), qualification_id,
            expected_model_hashes, f"cache concurrency report for {family}",
        )
        performance_records = [
            run
            for workload in performance.get("workloads", [])
            for run in workload.get("appleRuns", [])
        ]
        validate_execution_models(
            performance_records, qualification_id, expected_model_hashes,
            f"performance report for {family}",
        )
        if (
            not lifecycle.get("passed")
            or lifecycle.get("lifecycleMode") != "pages"
            or int(lifecycle.get("measuredCycles", 0)) < 100
        ):
            raise RuntimeError(f"100-cycle lifecycle gate failed for {family}")
        if lifecycle.get("reportSha256") != report_hash(lifecycle):
            raise RuntimeError(f"lifecycle report hash mismatch for {family}")
        validate_execution_models(
            [lifecycle], qualification_id, expected_model_hashes,
            f"lifecycle report for {family}",
        )
        maximum_growth = int(
            acceptance["performance"]["maximumResidentGrowthAfter100PagesBytes"]
        )
        if int(lifecycle["residentBytes"]["growth"]) > maximum_growth:
            raise RuntimeError(f"resident growth exceeds acceptance for {family}")

        provenance = model["models"]
        artifact_id = str(provenance["artifactId"])
        hashes = (
            str(provenance["detection"]["packageSha256"]),
            str(provenance["recognition"]["packageSha256"]),
        )
        if model_artifact_id is None:
            model_artifact_id = artifact_id
            model_hashes = hashes
        elif artifact_id != model_artifact_id or hashes != model_hashes:
            raise RuntimeError("qualified devices did not use identical model artifacts")

        families.add(family)
        devices.append({
            "deviceFamily": family,
            "deviceBrand": brand,
            "operatingSystem": identity.get("operatingSystem"),
            "runnerLabel": identity.get("runnerLabel"),
            "modelQualificationReportSha256": model["reportSha256"],
            "qualityReportSha256": quality["reportSha256"],
            "performanceReportSha256": performance["reportSha256"],
            "cacheConcurrencyReportSha256": cache["reportSha256"],
            "lifecycleReportSha256": lifecycle["reportSha256"],
            "lifecycleGrowthBytes": lifecycle["residentBytes"]["growth"],
        })

    if len(devices) < minimum_devices:
        raise RuntimeError(
            f"qualification requires {minimum_devices} independent devices, got {len(devices)}"
        )
    expected_models = acceptance["models"]
    if (model_artifact_id != expected_models["artifactId"] or
            model_hashes != (
                expected_models["detectionPackageSha256"],
                expected_models["recognitionPackageSha256"],
            )):
        raise RuntimeError("qualified model artifacts differ from the locked acceptance")
    result = {
        "schema": "light-ocr-apple-provider-baselines/1.0",
        "status": "candidate",
        "qualificationId": qualification_id,
        "generatedFromCommit": arguments.git_commit,
        "acceptanceSha256": acceptance_sha256,
        "modelArtifactId": model_artifact_id,
        "modelPackageSha256": {
            "detection": model_hashes[0] if model_hashes else None,
            "recognition": model_hashes[1] if model_hashes else None,
        },
        "qualifiedDeviceFamilies": sorted(families),
        "devices": devices,
    }
    encoded = json.dumps(
        result, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    )
    result["reportSha256"] = hashlib.sha256(encoded.encode("utf-8")).hexdigest()
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({
        "passed": True,
        "devices": len(devices),
        "families": sorted(families),
        "output": str(arguments.output),
    }, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
