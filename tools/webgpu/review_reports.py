#!/usr/bin/env python3
"""Mechanically validate a Linux/Windows Native WebGPU report pair."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any

try:
    from . import build_runtime, qualify
except ImportError:  # Direct script execution.
    import build_runtime  # type: ignore[no-redef]
    import qualify  # type: ignore[no-redef]


PLATFORMS = ("linux-x64", "windows-x64")
HEX40 = re.compile(r"[0-9a-f]{40}")
HEX64 = re.compile(r"[0-9a-f]{64}")
REPORT_FIELDS = {
    "schemaVersion",
    "evidenceId",
    "platformId",
    "sourceRevision",
    "buildProvenance",
    "sdk",
    "nativePackage",
    "host",
    "fixtureContract",
    "cases",
    "profiles",
    "gates",
    "passed",
}
MANUAL_REVIEW_CHECKLIST = (
    "Confirm the reported adapters, operating systems, drivers, and power/thermal conditions support the proposed compatibility scope.",
    "Inspect FP32 ORT profiles and operator counts, including every bounded CPU partition and the strict fail-closed evidence.",
    "Review latency distributions, CPU-s/average cores, cold starts, RSS, and available device-memory evidence; RSS is not a VRAM measurement.",
    "Inspect raw logs for driver, Dawn, validation, device-loss, and teardown warnings not represented by a failed mechanical gate.",
    "Choose device-specific, vendor-scoped, Preview, qualification-only, or rejected status; one report per platform does not prove a cross-vendor claim.",
    "Only after review, bind both report hashes and platform artifact-set hashes in the production lock and rerun release validation.",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validated_text_evidence_size(
    path: Path, expected_digest: str, *, platform_id: str, context: str
) -> int:
    """Validate tracked JSON whose Windows CRLF may be normalized by Git."""
    data = path.read_bytes()
    candidates = [data]
    if platform_id == "windows-x64":
        normalized = data.replace(b"\r\n", b"\n")
        crlf = normalized.replace(b"\n", b"\r\n")
        if crlf != data:
            candidates.append(crlf)
    for candidate in candidates:
        if hashlib.sha256(candidate).hexdigest() == expected_digest:
            return len(candidate)
    raise RuntimeError(f"{platform_id} {context} hash mismatch")


def read_json(path: Path, context: str) -> dict[str, Any]:
    if path.is_symlink() or not path.is_file():
        raise RuntimeError(f"{context} must be a regular file: {path}")
    try:
        value = json.loads(path.read_text("utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exception:
        raise RuntimeError(f"cannot read {context}: {path}: {exception}") from exception
    if not isinstance(value, dict):
        raise RuntimeError(f"{context} must be a JSON object: {path}")
    return value


def mapping(value: object, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise RuntimeError(f"{context} must be an object")
    return value


def list_of_mappings(value: object, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list) or not all(isinstance(item, dict) for item in value):
        raise RuntimeError(f"{context} must be an array of objects")
    return value


def list_of_strings(value: object, context: str) -> list[str]:
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise RuntimeError(f"{context} must be an array of strings")
    return value


def validate_report_qualification(
    value: object, lock_qualification: dict[str, Any], *, platform_id: str
) -> None:
    """Validate the pending qualification snapshot captured by a device run."""
    qualification = mapping(value, f"{platform_id} evidence qualification")
    if (
        set(qualification) != set(lock_qualification)
        or qualification.get("status")
        != "development-pending-device-validation"
        or qualification.get("evidenceId") != lock_qualification.get("evidenceId")
        or qualification.get("providerGatePassed") is not False
        or qualification.get("productionArtifactQualified") is not False
        or qualification.get("qualifiedArtifactSetSha256")
        != {"linux-x64": None, "windows-x64": None}
        or qualification.get("qualificationReportSha256")
        != {"linux-x64": None, "windows-x64": None}
        or qualification.get("requiredPlatforms")
        != lock_qualification.get("requiredPlatforms")
        or not isinstance(qualification.get("knownLimitations"), list)
        or not all(
            isinstance(item, str) and item
            for item in qualification["knownLimitations"]
        )
    ):
        raise RuntimeError(
            f"{platform_id} evidence qualification snapshot is invalid"
        )


def current_revision() -> str:
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=qualify.ROOT,
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    ).stdout.strip()
    if not HEX40.fullmatch(revision):
        raise RuntimeError("current Git revision is not a full SHA-1")
    return revision


def canonical_hash(value: dict[str, Any]) -> str:
    candidate = dict(value)
    candidate.pop("reportSha256", None)
    encoded = json.dumps(
        candidate, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    )
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def expected_case_keys() -> set[str]:
    keys = {
        f"{fixture}:{mode}"
        for fixture in qualify.DEFAULT_FIXTURES
        for mode in ("cpu", "allow", "strict")
    }
    canary = qualify.DEFAULT_FIXTURES[0]
    keys.update({f"{canary}:auto", f"{canary}:lifecycle", "native-cpp:auto"})
    return keys


def expected_profile_keys() -> set[str]:
    keys = {
        f"{fixture}:{mode}"
        for fixture in qualify.DEFAULT_FIXTURES
        for mode in ("allow",)
    }
    canary = qualify.DEFAULT_FIXTURES[0]
    keys.update({f"{canary}:auto", f"{canary}:lifecycle", "native-cpp:auto"})
    return keys


def expected_gate_names() -> set[str]:
    names = {
        "build-provenance",
        "runtime-contract",
        "native-payload-size",
        "graphics-driver-identity",
        "fixture-corpus-contract",
        "measurement-sample-contract",
        "d112-auto",
        "aggregate-allow-p50-speedup",
        "target-fixture-p50-speedup",
        "native-cpp-auto",
        "native-cpp-cold-start",
        "native-cpp-memory",
        "repeated-lifecycle",
    }
    for fixture in qualify.DEFAULT_FIXTURES:
        names.update(
            {
                f"{fixture}-cpu-provider",
                f"{fixture}-cpu-determinism",
                f"{fixture}-allow-provider",
                f"{fixture}-allow-quality",
                f"{fixture}-allow-determinism",
                f"{fixture}-strict-fail-closed",
                f"{fixture}-allow-p95",
            }
        )
    for key in expected_case_keys():
        if not key.endswith((":cpu", ":strict")) and key != "native-cpp:auto":
            names.add(f"{key}-memory")
    canary = qualify.DEFAULT_FIXTURES[0]
    for mode in ("cpu", "allow", "auto"):
        names.add(f"{canary}:{mode}-cold-start")
    for key in expected_profile_keys():
        names.add(f"{key}-profile-placement")
        names.add(f"{key}-cpu-operator-allowlist")
    return names


def validate_file_record(record: object, context: str) -> dict[str, Any]:
    value = mapping(record, context)
    if set(value) != {"path", "bytes", "sha256"}:
        raise RuntimeError(f"{context} fields are invalid")
    path = value.get("path")
    size = value.get("bytes")
    digest = value.get("sha256")
    if (
        not isinstance(path, str)
        or not path
        or PurePosixPath(path).is_absolute()
        or ".." in PurePosixPath(path).parts
        or type(size) is not int
        or size < 1
        or not isinstance(digest, str)
        or not HEX64.fullmatch(digest)
    ):
        raise RuntimeError(f"{context} identity is invalid")
    return value


def validate_manifest(
    path: Path,
    *,
    platform_id: str,
    report: dict[str, Any],
    lock: dict[str, object],
) -> dict[str, Any]:
    manifest = read_json(path, "copied SDK artifact manifest")
    expected_fields = {
        "schemaVersion",
        "contractId",
        "platform",
        "runtime",
        "artifacts",
        "headers",
        "packages",
        "sessionOptions",
        "qualification",
    }
    if set(manifest) != expected_fields or manifest.get("schemaVersion") != 2:
        raise RuntimeError(f"{platform_id} SDK manifest schema is invalid")
    platform_lock = mapping(
        mapping(lock["platforms"], "lock.platforms")[platform_id], "platform lock"
    )
    expected_platform: dict[str, object] = {
        "id": platform_id,
        "operatingSystem": platform_lock["operatingSystem"],
        "architecture": platform_lock["architecture"],
    }
    if "libc" in platform_lock:
        expected_platform["libc"] = platform_lock["libc"]
    lock_qualification = mapping(
        lock["qualification"], "runtime lock qualification"
    )
    if (
        manifest.get("contractId") != lock["contractId"]
        or manifest.get("platform") != expected_platform
        or manifest.get("runtime") != build_runtime.runtime_identity(lock, platform_id)
        or manifest.get("sessionOptions") != lock["sessionOptions"]
    ):
        raise RuntimeError(
            f"{platform_id} SDK manifest disagrees with the runtime lock"
        )
    validate_report_qualification(
        manifest.get("qualification"), lock_qualification, platform_id=platform_id
    )

    artifacts = mapping(manifest.get("artifacts"), f"{platform_id} manifest.artifacts")
    if set(artifacts) != {
        "linkLibrary",
        "providerLibrary",
        "runtimeFiles",
        "files",
        "artifactSetSha256",
    }:
        raise RuntimeError(f"{platform_id} manifest artifact fields are invalid")
    records = list_of_mappings(
        artifacts.get("files"), f"{platform_id} manifest.artifacts.files"
    )
    plan = {
        item["outputPath"]: item
        for item in build_runtime.artifact_plan(lock, platform_id)
    }
    observed: set[str] = set()
    for index, record in enumerate(records):
        relative = record.get("path")
        expected = plan.get(relative) if isinstance(relative, str) else None
        if (
            expected is None
            or relative in observed
            or type(record.get("bytes")) is not int
            or record["bytes"] < 1
            or not isinstance(record.get("sha256"), str)
            or not HEX64.fullmatch(record["sha256"])
            or record.get("role") != expected["role"]
            or record.get("sourcePackage") != expected["package"]
            or record.get("sourcePath") != expected["sourcePath"]
        ):
            raise RuntimeError(
                f"{platform_id} manifest artifact record {index} is invalid"
            )
        observed.add(relative)
    if observed != set(plan):
        raise RuntimeError(f"{platform_id} manifest artifact inventory is incomplete")
    runtime_specs = list_of_mappings(
        platform_lock.get("runtimeFiles"), f"{platform_id} runtime lock files"
    )
    expected_runtime_paths = [record["outputPath"] for record in runtime_specs]
    expected_provider_path = next(
        record["outputPath"]
        for record in runtime_specs
        if record.get("role") == "webgpu-plugin"
    )
    link_library = mapping(
        platform_lock.get("linkLibrary"), f"{platform_id} link library"
    )
    if (
        artifacts.get("runtimeFiles") != expected_runtime_paths
        or artifacts.get("providerLibrary") != expected_provider_path
        or artifacts.get("linkLibrary") != link_library.get("outputPath")
    ):
        raise RuntimeError(f"{platform_id} manifest loader paths are invalid")
    header_records = [record for record in records if record.get("role") == "header"]
    if manifest.get("headers") != {
        "directory": "include",
        "onnxruntimeVersion": "1.24.4",
        "files": header_records,
    }:
        raise RuntimeError(f"{platform_id} manifest header identity is invalid")
    package_lock = mapping(lock["packages"], "runtime lock packages")
    expected_packages = [
        {
            "name": name,
            **{
                key: package_lock[name][key]
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
        for name in ("onnxruntime", "webgpu")
    ]
    if manifest.get("packages") != expected_packages:
        raise RuntimeError(f"{platform_id} manifest package identity is invalid")
    artifact_set = artifacts.get("artifactSetSha256")
    report_sdk = mapping(report.get("sdk"), f"{platform_id} report.sdk")
    if (
        artifact_set != build_runtime.artifact_set_digest(records)
        or artifact_set != report_sdk.get("artifactSetSha256")
        or not isinstance(report_sdk.get("manifestSha256"), str)
    ):
        raise RuntimeError(f"{platform_id} SDK artifact identity mismatch")
    validated_text_evidence_size(
        path,
        report_sdk["manifestSha256"],
        platform_id=platform_id,
        context="SDK manifest",
    )
    return manifest


def validate_descriptor(
    path: Path,
    *,
    platform_id: str,
    report: dict[str, Any],
    manifest: dict[str, Any],
) -> dict[str, Any]:
    descriptor = read_json(path, "copied native runtime descriptor")
    if set(descriptor) != {
        "schemaVersion",
        "platform",
        "runtime",
        "qualificationOnly",
        "released",
        "autoPolicy",
        "providers",
        "addon",
    }:
        raise RuntimeError(f"{platform_id} runtime descriptor fields are invalid")
    platform_os = "linux" if platform_id == "linux-x64" else "win32"
    expected_platform: dict[str, object] = {
        "id": platform_id,
        "os": platform_os,
        "architecture": "x86_64",
    }
    if platform_id == "linux-x64":
        expected_platform["libc"] = "glibc"
    if (
        descriptor.get("schemaVersion") != "2.0"
        or descriptor.get("platform") != expected_platform
        or descriptor.get("qualificationOnly") is not True
        or descriptor.get("released") is not False
        or descriptor.get("autoPolicy")
        != {"id": f"{platform_id}-v1", "version": 1, "providers": ["webgpu", "cpu"]}
    ):
        raise RuntimeError(f"{platform_id} runtime descriptor policy is invalid")

    runtime = mapping(descriptor.get("runtime"), f"{platform_id} descriptor.runtime")
    runtime_records = [
        validate_file_record(record, f"{platform_id} runtime artifact {index}")
        for index, record in enumerate(
            list_of_mappings(runtime.get("artifacts"), "runtime artifacts")
        )
    ]
    if {key: runtime.get(key) for key in ("flavor", "kind", "version", "abi")} != {
        "flavor": "webgpu",
        "kind": "onnxruntime-plugin-webgpu",
        "version": "1.24.4",
        "abi": "onnxruntime-c-api-24-plugin-ep-0.1",
    }:
        raise RuntimeError(f"{platform_id} runtime descriptor ABI is invalid")
    manifest_records = {
        record["path"]: record
        for record in mapping(manifest["artifacts"], "manifest artifacts")["files"]
    }
    runtime_paths = list_of_strings(
        mapping(manifest["artifacts"], "manifest artifacts").get("runtimeFiles"),
        f"{platform_id} runtime paths",
    )
    expected_runtime_records = []
    for relative in runtime_paths:
        manifest_record = manifest_records[relative]
        expected_runtime_records.append(
            {
                "path": f"native/{PurePosixPath(relative).name}",
                "bytes": manifest_record["bytes"],
                "sha256": manifest_record["sha256"],
            }
        )
    if runtime_records != expected_runtime_records:
        raise RuntimeError(f"{platform_id} descriptor runtime artifacts mismatch SDK")

    addon = validate_file_record(descriptor.get("addon"), f"{platform_id} addon")
    providers = mapping(descriptor.get("providers"), f"{platform_id} providers")
    if set(providers) != {"webgpu", "cpu"}:
        raise RuntimeError(f"{platform_id} descriptor providers are invalid")
    webgpu = mapping(providers["webgpu"], f"{platform_id} WebGPU provider")
    cpu = mapping(providers["cpu"], f"{platform_id} CPU provider")
    webgpu_records = runtime_records[1:]
    if (
        webgpu.get("runtimeProvider") != "WebGpuExecutionProvider"
        or webgpu.get("providerVersion") != "0.1.0"
        or webgpu.get("qualificationId") != report.get("evidenceId")
        or webgpu.get("providerLibrary") != webgpu_records[0]
        or webgpu.get("artifacts") != webgpu_records
        or cpu
        != {
            "runtimeProvider": "CPUExecutionProvider",
            "qualificationId": "cpu-baseline-v1",
            "artifacts": [runtime_records[0]],
        }
    ):
        raise RuntimeError(f"{platform_id} descriptor provider identity is invalid")

    native = mapping(report.get("nativePackage"), f"{platform_id} nativePackage")
    unique_payload = {record["path"]: record for record in [addon, *runtime_records]}
    descriptor_digest = native.get("descriptorSha256")
    if not isinstance(descriptor_digest, str):
        raise RuntimeError(f"{platform_id} native descriptor identity is invalid")
    descriptor_bytes = validated_text_evidence_size(
        path,
        descriptor_digest,
        platform_id=platform_id,
        context="runtime descriptor",
    )
    payload_bytes = descriptor_bytes + sum(
        int(record["bytes"]) for record in unique_payload.values()
    )
    if (
        native.get("payloadBytes") != payload_bytes
        or native.get("payloadCeilingBytes") != qualify.MAX_NATIVE_PAYLOAD_BYTES
        or payload_bytes > qualify.MAX_NATIVE_PAYLOAD_BYTES
    ):
        raise RuntimeError(f"{platform_id} native payload identity is invalid")
    return descriptor


def recompute_gates(
    report: dict[str, Any],
    *,
    platform_id: str,
    manifest_path: Path,
    descriptor_path: Path,
) -> list[dict[str, Any]]:
    native = mapping(report.get("nativePackage"), f"{platform_id} native package")
    payload_bytes = native.get("payloadBytes")
    if (
        type(payload_bytes) is not int
        or payload_bytes <= descriptor_path.stat().st_size
    ):
        raise RuntimeError(f"{platform_id} native payload byte count is invalid")
    with tempfile.TemporaryDirectory(prefix="light-ocr-webgpu-review-") as directory:
        root = Path(directory)
        sdk = root / "sdk"
        package = root / "native-package" / "native"
        sdk.mkdir()
        package.mkdir(parents=True)
        shutil.copy2(manifest_path, sdk / "artifact-manifest.json")
        shutil.copy2(descriptor_path, package / "runtime-descriptor.json")
        recomputed = qualify.collect_evidence(
            platform_id=platform_id,
            sdk=sdk,
            native=root / "native-package",
            cases=mapping(report.get("cases"), f"{platform_id} cases"),
            profiles=mapping(report.get("profiles"), f"{platform_id} profiles"),
            graphics=mapping(
                mapping(report.get("host"), f"{platform_id} host").get("graphics"),
                f"{platform_id} graphics",
            ),
            rebuilt_from_source=True,
            native_payload_bytes_override=payload_bytes,
        )
    gates = list_of_mappings(recomputed.get("gates"), "recomputed gates")
    if recomputed.get("passed") is not True:
        raise RuntimeError(f"{platform_id} recomputed mechanical gates failed")
    return gates


def validate_report_directory(
    directory: Path,
    *,
    platform_id: str,
    expected_revision: str | None,
    lock: dict[str, object],
) -> dict[str, Any]:
    if directory.is_symlink() or not directory.is_dir():
        raise RuntimeError(f"{platform_id} report directory must be a real directory")
    report_path = directory / "qualification-report.json"
    report = read_json(report_path, f"{platform_id} qualification report")
    sidecar_path = directory / "qualification-report.sha256"
    if sidecar_path.is_symlink() or not sidecar_path.is_file():
        raise RuntimeError(f"{platform_id} report sidecar must be a regular file")
    try:
        sidecar = sidecar_path.read_text("utf-8")
    except (OSError, UnicodeError) as exception:
        raise RuntimeError(f"cannot read {platform_id} report sidecar") from exception
    match = re.fullmatch(r"([0-9a-f]{64})  qualification-report\.json\n", sidecar)
    if match is None:
        raise RuntimeError(f"{platform_id} qualification report hash mismatch")
    report_digest = match.group(1)
    validated_text_evidence_size(
        report_path,
        report_digest,
        platform_id=platform_id,
        context="qualification report",
    )
    qualification = mapping(lock["qualification"], "runtime lock qualification")
    source_revision = report.get("sourceRevision")
    if (
        set(report) != REPORT_FIELDS
        or report.get("schemaVersion") != "1.1"
        or report.get("platformId") != platform_id
        or not isinstance(source_revision, str)
        or not HEX40.fullmatch(source_revision)
        or (expected_revision is not None and source_revision != expected_revision)
        or report.get("evidenceId") != qualification.get("evidenceId")
        or report.get("passed") is not True
        or report.get("buildProvenance")
        != {"rebuiltFromSource": True, "qualificationEligible": True}
    ):
        raise RuntimeError(f"{platform_id} qualification report identity is invalid")
    fixture_contract = mapping(
        report.get("fixtureContract"), f"{platform_id} fixture contract"
    )
    if fixture_contract != {
        "required": list(qualify.DEFAULT_FIXTURES),
        "observed": sorted(qualify.DEFAULT_FIXTURES),
    }:
        raise RuntimeError(f"{platform_id} fixture contract is invalid")
    cases = mapping(report.get("cases"), f"{platform_id} cases")
    profiles = mapping(report.get("profiles"), f"{platform_id} profiles")
    if (
        set(cases) != expected_case_keys()
        or set(profiles) != expected_profile_keys()
        or not all(isinstance(value, dict) for value in cases.values())
        or not all(isinstance(value, dict) for value in profiles.values())
    ):
        raise RuntimeError(f"{platform_id} case/profile inventory is incomplete")
    gates = list_of_mappings(report.get("gates"), f"{platform_id} gates")
    gate_names = [gate.get("name") for gate in gates]
    if (
        not all(isinstance(name, str) for name in gate_names)
        or len(gate_names) != len(set(gate_names))
        or set(gate_names) != expected_gate_names()
        or any(
            set(gate) != {"name", "passed", "detail"}
            or gate.get("passed") is not True
            or not isinstance(gate.get("detail"), str)
            for gate in gates
        )
    ):
        raise RuntimeError(f"{platform_id} mechanical gate inventory is invalid")
    host = mapping(report.get("host"), f"{platform_id} host")
    graphics = mapping(host.get("graphics"), f"{platform_id} graphics")
    if not isinstance(graphics.get("adapters"), list) or not graphics["adapters"]:
        raise RuntimeError(f"{platform_id} report contains no graphics adapter")

    artifacts = directory / "artifacts"
    if artifacts.is_symlink() or not artifacts.is_dir():
        raise RuntimeError(f"{platform_id} artifact evidence must be a real directory")
    manifest = validate_manifest(
        artifacts / "sdk-artifact-manifest.json",
        platform_id=platform_id,
        report=report,
        lock=lock,
    )
    descriptor_path = artifacts / "native-runtime-descriptor.json"
    validate_descriptor(
        descriptor_path,
        platform_id=platform_id,
        report=report,
        manifest=manifest,
    )
    recomputed_gates = recompute_gates(
        report,
        platform_id=platform_id,
        manifest_path=artifacts / "sdk-artifact-manifest.json",
        descriptor_path=descriptor_path,
    )
    if {gate["name"]: gate["passed"] for gate in gates} != {
        gate["name"]: gate["passed"] for gate in recomputed_gates
    }:
        raise RuntimeError(f"{platform_id} report gates differ from recomputation")
    return {
        "sourceRevision": source_revision,
        "reportSha256": report_digest,
        "artifactSetSha256": report["sdk"]["artifactSetSha256"],
        "sdkManifestSha256": report["sdk"]["manifestSha256"],
        "runtimeDescriptorSha256": report["nativePackage"]["descriptorSha256"],
        "nativePayloadBytes": report["nativePackage"]["payloadBytes"],
        "gateCount": len(gates),
        "host": host,
    }


def collect_pair(
    reports_root: Path,
    *,
    expected_revision: str | None = None,
    lock_path: Path = build_runtime.DEFAULT_LOCK,
) -> dict[str, Any]:
    if expected_revision is not None and not HEX40.fullmatch(expected_revision):
        raise RuntimeError("expected source revision must be a full lowercase SHA-1")
    lock = build_runtime.load_lock(lock_path)
    build_runtime.validate_lock(lock)
    qualification = mapping(lock["qualification"], "runtime lock qualification")
    pending = (
        qualification.get("status") == "development-pending-device-validation"
        and qualification.get("providerGatePassed") is False
        and qualification.get("productionArtifactQualified") is False
    )
    production = (
        qualification.get("status") == "production-qualified"
        and qualification.get("providerGatePassed") is True
        and qualification.get("productionArtifactQualified") is True
    )
    if not pending and not production:
        raise RuntimeError("report collection requires a valid qualification lock")
    root = reports_root.resolve()
    try:
        platforms = {
            platform_id: validate_report_directory(
                root / platform_id,
                platform_id=platform_id,
                expected_revision=expected_revision,
                lock=lock,
            )
            for platform_id in PLATFORMS
        }
    except (IndexError, KeyError, TypeError, ValueError) as exception:
        raise RuntimeError("qualification report structure is invalid") from exception
    if production:
        artifact_hashes = {
            platform_id: platform["artifactSetSha256"]
            for platform_id, platform in platforms.items()
        }
        report_hashes = {
            platform_id: platform["reportSha256"]
            for platform_id, platform in platforms.items()
        }
        if (
            qualification.get("qualifiedArtifactSetSha256") != artifact_hashes
            or qualification.get("qualificationReportSha256") != report_hashes
        ):
            raise RuntimeError(
                "production qualification lock differs from the reviewed reports"
            )
    candidate: dict[str, Any] = {
        "schema": "light-ocr-webgpu-provider-gate-review/1.0",
        "status": (
            "production-qualified" if production else "manual-review-required"
        ),
        "mechanicalValidationPassed": True,
        "evidenceId": qualification["evidenceId"],
        "sourceRevisions": {
            platform_id: platform["sourceRevision"]
            for platform_id, platform in platforms.items()
        },
        "platforms": platforms,
        "manualReviewChecklist": list(MANUAL_REVIEW_CHECKLIST),
    }
    candidate["reportSha256"] = canonical_hash(candidate)
    return candidate


def write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent
    )
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        temporary.write_text(
            json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            "utf-8",
        )
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reports-root", type=Path, required=True)
    parser.add_argument("--source-revision", default=None)
    parser.add_argument("--runtime-lock", type=Path, default=build_runtime.DEFAULT_LOCK)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    qualify.require_clean_source()
    candidate = collect_pair(
        arguments.reports_root,
        expected_revision=arguments.source_revision,
        lock_path=arguments.runtime_lock.resolve(),
    )
    write_json_atomic(arguments.output.resolve(), candidate)
    print(
        json.dumps(
            {
                "mechanicalValidationPassed": True,
                "output": str(arguments.output.resolve()),
                "reportSha256": candidate["reportSha256"],
                "status": candidate["status"],
            },
            ensure_ascii=False,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.SubprocessError) as exception:
        print(f"error: {exception}", file=sys.stderr)
        raise SystemExit(2)
