#!/usr/bin/env python3
"""Create a self-contained Apple provider model bundle from locked artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BASE = ROOT / "models" / "generated" / "ppocrv6-small-webgpu-20260719.1"
DEFAULT_APPLE = ROOT / "models" / "generated" / "apple-fp16-20260715.1"
DEFAULT_OUTPUT = ROOT / "models" / "generated" / "ppocrv6-small-native-20260719.1"
ACCEPTANCE = ROOT / "tools" / "apple" / "acceptance.json"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inventory(root: Path) -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        if relative in {"manifest.json", "SHA256SUMS"}:
            continue
        result[relative] = {"bytes": path.stat().st_size, "sha256": sha256(path)}
    return result


def checksum_inventory(root: Path) -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        if relative == "SHA256SUMS":
            continue
        result[relative] = {"bytes": path.stat().st_size, "sha256": sha256(path)}
    return result


def report_hash(report: dict[str, object]) -> str:
    value = dict(report)
    value.pop("reportSha256", None)
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    )
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def accepted_device_families(
    path: Path, acceptance: dict[str, object], acceptance_sha256: str
) -> list[str]:
    report = json.loads(path.read_text("utf-8"))
    if not isinstance(report, dict):
        raise RuntimeError("Apple provider baseline is not an object")
    if (
        report.get("schema") != "light-ocr-apple-provider-baselines/1.0"
        or report.get("status") != "accepted"
        or report.get("reportSha256") != report_hash(report)
    ):
        raise RuntimeError("Apple provider baseline is not an accepted intact report")
    approval = str(report.get("approvedByCommit", ""))
    candidate_hash = str(report.get("candidateReportSha256", ""))
    if (
        len(approval) != 40
        or any(value not in "0123456789abcdef" for value in approval)
        or len(candidate_hash) != 64
        or any(value not in "0123456789abcdef" for value in candidate_hash)
    ):
        raise RuntimeError("Apple provider baseline is missing review provenance")
    candidate = dict(report)
    candidate["status"] = "candidate"
    candidate.pop("approvedByCommit", None)
    candidate.pop("candidateReportSha256", None)
    candidate["reportSha256"] = candidate_hash
    if report_hash(candidate) != candidate_hash:
        raise RuntimeError("Apple provider baseline is not linked to its reviewed candidate")
    models = acceptance["models"]
    if (
        report.get("qualificationId") != acceptance["qualificationId"]
        or report.get("acceptanceSha256") != acceptance_sha256
        or report.get("modelArtifactId") != models["artifactId"]
        or report.get("modelPackageSha256") != {
            "detection": models["detectionPackageSha256"],
            "recognition": models["recognitionPackageSha256"],
        }
    ):
        raise RuntimeError("Apple provider baseline does not match the locked acceptance")
    families = report.get("qualifiedDeviceFamilies")
    devices = report.get("devices")
    minimum = int(acceptance["compatibility"]["minimumQualifiedDevices"])
    if (
        not isinstance(families, list)
        or not isinstance(devices, list)
        or len(families) < minimum
        or families != sorted(set(families))
        or sorted(device.get("deviceFamily") for device in devices) != families
    ):
        raise RuntimeError("Apple provider baseline has invalid device coverage")
    allowed = {"Apple M1", "Apple M2", "Apple M3", "Apple M4"}
    if any(family not in allowed for family in families):
        raise RuntimeError("Apple provider baseline contains an unsupported device family")
    return families


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, default=DEFAULT_BASE)
    parser.add_argument("--apple", type=Path, default=DEFAULT_APPLE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--qualification-id")
    parser.add_argument(
        "--qualification-report",
        type=Path,
        help="Reviewed contracts/apple-provider-baselines.json used by release packaging",
    )
    parser.add_argument(
        "--validated-device-family",
        action="append",
        dest="validated_device_families",
        choices=("Apple M1", "Apple M2", "Apple M3", "Apple M4"),
        default=None,
        help="Device family with reviewed performance evidence; may be repeated",
    )
    parser.add_argument(
        "--device-policy",
        choices=("open-macos", "validated-only"),
        default="open-macos",
        help="Runtime device policy; production defaults to open macOS compatibility",
    )
    arguments = parser.parse_args()
    if arguments.qualification_report and arguments.validated_device_families:
        parser.error(
            "--qualification-report and --validated-device-family are mutually exclusive"
        )
    acceptance_bytes = ACCEPTANCE.read_bytes()
    acceptance = json.loads(acceptance_bytes)
    if arguments.qualification_report:
        validated_device_families = accepted_device_families(
            arguments.qualification_report.resolve(), acceptance,
            hashlib.sha256(acceptance_bytes).hexdigest(),
        )
    else:
        validated_device_families = arguments.validated_device_families or ["Apple M4"]
    if len(validated_device_families) != len(set(validated_device_families)):
        parser.error("--validated-device-family values must be unique")
    base = arguments.base.resolve()
    apple = arguments.apple.resolve()
    output = arguments.output.resolve()
    provenance = json.loads((apple / "provenance.json").read_text("utf-8"))
    locked_models = acceptance["models"]
    routing = acceptance["routing"]
    qualification_id = arguments.qualification_id or acceptance["qualificationId"]
    if qualification_id != acceptance["qualificationId"]:
        parser.error("--qualification-id must match the locked acceptance")
    if (
        provenance.get("artifactId") != locked_models["artifactId"]
        or provenance.get("detection", {}).get("packageSha256")
        != locked_models["detectionPackageSha256"]
        or provenance.get("recognition", {}).get("packageSha256")
        != locked_models["recognitionPackageSha256"]
    ):
        raise RuntimeError("Apple model artifacts differ from the locked acceptance")

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=output.name + ".", dir=output.parent) as work:
        temporary = Path(work) / output.name
        shutil.copytree(base, temporary)
        for package in (provenance["detection"]["package"], provenance["recognition"]["package"]):
            shutil.copytree(apple / package, temporary / "apple" / package)
        shutil.copy2(apple / "provenance.json", temporary / "apple" / "provenance.json")

        manifest_path = temporary / "manifest.json"
        manifest = json.loads(manifest_path.read_text("utf-8"))
        normalized_path = temporary / manifest["normalizedConfigPath"]
        normalized = json.loads(normalized_path.read_text("utf-8"))
        normalized["bundleId"] = output.name
        normalized_path.write_text(
            json.dumps(normalized, ensure_ascii=False, sort_keys=True, separators=(",", ":")),
            encoding="utf-8",
        )
        if (
            manifest.get("schemaVersion") != "1.2"
            or manifest.get("providers", {}).get("webgpu", {}).get("precision")
            != "fp16"
        ):
            raise RuntimeError("base bundle does not contain the WebGPU FP16 contract")
        manifest["bundleId"] = output.name
        manifest["coreCompatibility"]["minimum"] = "0.3.0"
        manifest["providers"]["apple"] = {
            "schemaVersion": "1.1",
            "minimumMacOS": "15.0",
            "devicePolicy": arguments.device_policy,
            "architectures": ["arm64", "x86_64"],
            "validatedDeviceFamilies": validated_device_families,
            "qualificationId": qualification_id,
            "detection": {
                **provenance["detection"],
                "packagePath": "apple/" + provenance["detection"]["package"],
                "preferredComputeUnit": "ane",
                "strictComputeUnit": "gpu",
                "intelComputeUnit": "cpu+gpu",
                "qualifiedMLCPUOperations": {"ios18.relu": 1, "pad": 1},
            },
            "recognition": {
                **provenance["recognition"],
                "packagePath": "apple/" + provenance["recognition"]["package"],
                "widthMultiple": routing["recognitionWidthMultiple"],
                "aneMaximumWidth": routing["recognitionAneMaximumWidth"],
                "runtimeWidthBuckets": routing["recognitionRuntimeWidthBuckets"],
                "maximumCachedFunctions": routing["maximumCachedFunctions"],
                "intelComputeUnit": "cpu+gpu",
                "qualifiedMLCPUOperations": {
                    "ios18.cast": 1,
                    "ios18.conv": 3,
                    "ios18.relu": 3,
                    "pad": 3,
                },
            },
        }
        manifest["files"] = inventory(temporary)
        manifest_path.write_text(
            json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")),
            encoding="utf-8",
        )
        checksums = checksum_inventory(temporary)
        (temporary / "SHA256SUMS").write_text(
            "".join(f"{record['sha256']}  {path}\n" for path, record in checksums.items()),
            encoding="ascii",
        )
        shutil.rmtree(output, ignore_errors=True)
        shutil.move(temporary, output)
    print(json.dumps({"bundleId": output.name, "files": len(inventory(output))}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
