#!/usr/bin/env python3
"""Assemble and validate the locked ONNX Runtime Native WebGPU plugin SDK."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LOCK = Path(__file__).with_name("runtime-lock.json")
EXPECTED_CONTRACT_ID = "native-webgpu-plugin-0.1.0-ort-1.24.4-v1"
EXPECTED_PACKAGES = {
    "onnxruntime": {
        "id": "Microsoft.ML.OnnxRuntime",
        "version": "1.24.4",
        "filename": "microsoft.ml.onnxruntime.1.24.4.nupkg",
        "source": "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/1.24.4/microsoft.ml.onnxruntime.1.24.4.nupkg",
        "catalog": "https://api.nuget.org/v3/catalog0/data/2026.03.21.08.32.20/microsoft.ml.onnxruntime.1.24.4.json",
        "bytes": 125194303,
        "sha512": "f5dd415dfcafcb3a7461f10a08f0337ea22c1ba8f8af81316daabf7496075add181aecb0de3cabebebdb9f5da3afbe507480aaf34b76e1189409088ccc5c2eac",
        "license": "MIT",
    },
    "webgpu": {
        "id": "Microsoft.ML.OnnxRuntime.EP.WebGpu",
        "version": "0.1.0",
        "filename": "microsoft.ml.onnxruntime.ep.webgpu.0.1.0.nupkg",
        "source": "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.ep.webgpu/0.1.0/microsoft.ml.onnxruntime.ep.webgpu.0.1.0.nupkg",
        "catalog": "https://api.nuget.org/v3/catalog0/data/2026.05.27.20.11.11/microsoft.ml.onnxruntime.ep.webgpu.0.1.0.json",
        "bytes": 33754099,
        "sha512": "d048cfb4a687d82547338cdf36649c95dfac0a254a752e2e53b5f2faeccfacf4e7b2ed3125e03e5f58bc1c23c6c7cbe356fa513c99b3ad217f491ac1c80bb92a",
        "license": "MIT",
        "upstreamTag": "plugin-ep-webgpu/v0.1.0",
        "upstreamCommit": "d2ede0adeb300958cfb5a256c09d27c66c3a6d71",
        "minimumOnnxRuntimeVersion": "1.24.4",
    },
}
EXPECTED_TOPOLOGY = {
    "kind": "plugin-ep",
    "coreRuntime": "onnxruntime",
    "providers": ["cpu", "webgpu"],
    "providerName": "WebGpuExecutionProvider",
    "registrationName": "light-ocr-webgpu",
    "webgpuImplementation": "Dawn Native",
}
EXPECTED_HEADERS = [
    "onnxruntime_c_api.h",
    "onnxruntime_cxx_api.h",
    "onnxruntime_cxx_inline.h",
    "onnxruntime_env_config_keys.h",
    "onnxruntime_ep_c_api.h",
    "onnxruntime_ep_device_ep_metadata_keys.h",
    "onnxruntime_float16.h",
    "onnxruntime_run_options_config_keys.h",
    "onnxruntime_session_options_config_keys.h",
]
EXPECTED_SESSION_OPTIONS = {
    "preferredLayout": "NHWC",
    "enableGraphCapture": "0",
    "validationMode": "basic",
    "powerPreference": "high-performance",
    "deviceIdSupported": False,
}
HEX64 = re.compile(r"^[0-9a-f]{64}$")
HEX128 = re.compile(r"^[0-9a-f]{128}$")


class ContractError(RuntimeError):
    """A lock, package, SDK, or host identity violates the frozen contract."""


def load_json(path: Path, context: str) -> dict[str, object]:
    try:
        value = json.loads(path.read_text("utf-8"))
    except (OSError, json.JSONDecodeError) as exception:
        raise ContractError(f"cannot read {context} {path}: {exception}") from exception
    if not isinstance(value, dict):
        raise ContractError(f"{context} root must be an object")
    return value


def load_lock(path: Path = DEFAULT_LOCK) -> dict[str, object]:
    lock = load_json(path, "runtime lock")
    validate_lock(lock)
    return lock


def require_mapping(parent: dict[str, object], key: str) -> dict[str, object]:
    value = parent.get(key)
    if not isinstance(value, dict):
        raise ContractError(f"field {key!r} must be an object")
    return value


def require_string(parent: dict[str, object], key: str) -> str:
    value = parent.get(key)
    if not isinstance(value, str) or not value:
        raise ContractError(f"field {key!r} must be a non-empty string")
    return value


def values_match_exactly(value: object, expected: object) -> bool:
    if type(value) is not type(expected):
        return False
    if isinstance(expected, dict):
        return value.keys() == expected.keys() and all(
            values_match_exactly(value[key], item) for key, item in expected.items()
        )
    if isinstance(expected, list):
        return len(value) == len(expected) and all(
            values_match_exactly(actual, item) for actual, item in zip(value, expected)
        )
    return value == expected


def require_exact(value: object, expected: object, field: str) -> None:
    if not values_match_exactly(value, expected):
        raise ContractError(
            f"runtime lock field {field} must be exactly {expected!r}, got {value!r}"
        )


def safe_relative(value: object, field: str) -> str:
    if not isinstance(value, str) or not value or "\0" in value or "\\" in value:
        raise ContractError(f"{field} must be a safe POSIX relative path")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
        raise ContractError(f"{field} must be a safe POSIX relative path")
    return value


def validate_artifact_spec(
    value: object, field: str, *, role: bool
) -> dict[str, object]:
    if not isinstance(value, dict):
        raise ContractError(f"{field} must be an object")
    expected_keys = {"package", "sourcePath", "outputPath"}
    if role:
        expected_keys.add("role")
    if set(value) != expected_keys:
        raise ContractError(f"{field} fields are invalid")
    package = require_string(value, "package")
    if package not in EXPECTED_PACKAGES:
        raise ContractError(f"{field}.package is invalid")
    safe_relative(value.get("sourcePath"), f"{field}.sourcePath")
    safe_relative(value.get("outputPath"), f"{field}.outputPath")
    if role and not re.fullmatch(r"[a-z0-9-]+", require_string(value, "role")):
        raise ContractError(f"{field}.role is invalid")
    return value


def validate_platform(platform_id: str, value: object) -> None:
    if not isinstance(value, dict):
        raise ContractError(f"platform {platform_id} must be an object")
    required = {
        "operatingSystem",
        "architecture",
        "graphicsBackend",
        "linkLibrary",
        "runtimeFiles",
    }
    if platform_id == "linux-x64":
        required.add("libc")
        identity = ("linux", "x86_64", "glibc", "Vulkan")
        actual = (
            value.get("operatingSystem"),
            value.get("architecture"),
            value.get("libc"),
            value.get("graphicsBackend"),
        )
    elif platform_id == "windows-x64":
        identity = ("windows", "x86_64", "D3D12")
        actual = (
            value.get("operatingSystem"),
            value.get("architecture"),
            value.get("graphicsBackend"),
        )
    else:
        raise ContractError(f"unsupported WebGPU platform: {platform_id}")
    if set(value) != required or actual != identity:
        raise ContractError(f"platform {platform_id} identity is invalid")
    link = validate_artifact_spec(
        value.get("linkLibrary"), f"{platform_id}.linkLibrary", role=False
    )
    runtime_files = value.get("runtimeFiles")
    if not isinstance(runtime_files, list) or len(runtime_files) < 2:
        raise ContractError(f"platform {platform_id} runtimeFiles are invalid")
    roles: set[str] = set()
    outputs = {str(link["outputPath"])}
    for index, entry in enumerate(runtime_files):
        spec = validate_artifact_spec(
            entry, f"{platform_id}.runtimeFiles[{index}]", role=True
        )
        role_name = str(spec["role"])
        output = str(spec["outputPath"])
        if role_name in roles or (output in outputs and output != link["outputPath"]):
            raise ContractError(
                f"platform {platform_id} artifact identities are duplicated"
            )
        roles.add(role_name)
        outputs.add(output)
    expected_roles = (
        {"onnxruntime-core", "webgpu-plugin"}
        if platform_id == "linux-x64"
        else {"onnxruntime-core", "webgpu-plugin", "dawn-dxcompiler", "dawn-dxil"}
    )
    if roles != expected_roles:
        raise ContractError(f"platform {platform_id} runtime roles are invalid")


def validate_lock(lock: dict[str, object]) -> None:
    if set(lock) != {
        "schemaVersion",
        "contractId",
        "topology",
        "packages",
        "headers",
        "platforms",
        "sessionOptions",
        "qualification",
    }:
        raise ContractError("runtime lock fields are invalid")
    require_exact(lock.get("schemaVersion"), 2, "schemaVersion")
    require_exact(lock.get("contractId"), EXPECTED_CONTRACT_ID, "contractId")
    require_exact(lock.get("topology"), EXPECTED_TOPOLOGY, "topology")
    require_exact(lock.get("packages"), EXPECTED_PACKAGES, "packages")
    require_exact(lock.get("headers"), EXPECTED_HEADERS, "headers")
    require_exact(
        lock.get("sessionOptions"), EXPECTED_SESSION_OPTIONS, "sessionOptions"
    )
    platforms = require_mapping(lock, "platforms")
    if set(platforms) != {"linux-x64", "windows-x64"}:
        raise ContractError("runtime lock must contain Linux x64 and Windows x64")
    for platform_id, value in platforms.items():
        validate_platform(platform_id, value)
    qualification = require_mapping(lock, "qualification")
    if set(qualification) != {
        "status",
        "evidenceId",
        "providerGatePassed",
        "productionArtifactQualified",
        "qualifiedArtifactSetSha256",
        "qualificationReportSha256",
        "requiredPlatforms",
        "knownLimitations",
    }:
        raise ContractError("qualification fields are invalid")
    if (
        not re.fullmatch(r"[A-Za-z0-9._-]+", str(qualification.get("evidenceId", "")))
        or qualification.get("requiredPlatforms") != ["linux-x64", "windows-x64"]
        or not isinstance(qualification.get("knownLimitations"), list)
        or len(qualification["knownLimitations"]) < 2
        or not all(
            isinstance(item, str) and item for item in qualification["knownLimitations"]
        )
    ):
        raise ContractError("qualification identity is invalid")
    qualified_hashes = qualification.get("qualifiedArtifactSetSha256")
    report_hashes = qualification.get("qualificationReportSha256")
    pending = (
        qualification.get("status") == "development-pending-device-validation"
        and qualification.get("providerGatePassed") is False
        and qualification.get("productionArtifactQualified") is False
        and qualified_hashes == {"linux-x64": None, "windows-x64": None}
        and report_hashes == {"linux-x64": None, "windows-x64": None}
    )
    production = (
        qualification.get("status") == "production-qualified"
        and qualification.get("providerGatePassed") is True
        and qualification.get("productionArtifactQualified") is True
        and isinstance(qualified_hashes, dict)
        and set(qualified_hashes) == {"linux-x64", "windows-x64"}
        and all(
            isinstance(value, str) and HEX64.fullmatch(value)
            for value in qualified_hashes.values()
        )
        and isinstance(report_hashes, dict)
        and set(report_hashes) == {"linux-x64", "windows-x64"}
        and all(
            isinstance(value, str) and HEX64.fullmatch(value)
            for value in report_hashes.values()
        )
    )
    if not pending and not production:
        raise ContractError(
            "qualification must be consistently pending or production-qualified"
        )


def file_digest(path: Path, algorithm: str) -> str:
    digest = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_package(path: Path, package: dict[str, object]) -> None:
    if not path.is_file() or path.is_symlink():
        raise ContractError(f"locked package is missing or not a regular file: {path}")
    size = path.stat().st_size
    if size != package["bytes"]:
        raise ContractError(
            f"package byte count mismatch for {package['id']}: expected {package['bytes']}, got {size}"
        )
    digest = file_digest(path, "sha512")
    if digest != package["sha512"]:
        raise ContractError(f"package SHA-512 mismatch for {package['id']}")
    try:
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise ContractError(
                    f"package contains duplicate ZIP members: {package['id']}"
                )
            if archive.testzip() is not None:
                raise ContractError(
                    f"package ZIP integrity check failed: {package['id']}"
                )
    except zipfile.BadZipFile as exception:
        raise ContractError(
            f"package is not a valid ZIP archive: {package['id']}"
        ) from exception


def acquire_package(
    package: dict[str, object], cache_dir: Path, *, offline: bool
) -> Path:
    cache_dir.mkdir(parents=True, exist_ok=True)
    destination = cache_dir / str(package["filename"])
    if destination.exists():
        validate_package(destination, package)
        return destination
    if offline:
        raise ContractError(f"offline package cache is missing {package['filename']}")
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.", dir=cache_dir
    )
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        try:
            with (
                urllib.request.urlopen(str(package["source"])) as response,
                temporary.open("wb") as output,
            ):
                shutil.copyfileobj(response, output, length=1024 * 1024)
        except (OSError, urllib.error.URLError) as exception:
            raise ContractError(
                f"cannot download {package['id']}: {exception}"
            ) from exception
        validate_package(temporary, package)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination


def archive_member(archive: zipfile.ZipFile, name: str, context: str) -> bytes:
    safe_relative(name, context)
    try:
        info = archive.getinfo(name)
    except KeyError as exception:
        raise ContractError(f"locked package member is missing: {name}") from exception
    mode = info.external_attr >> 16
    if info.is_dir() or stat.S_ISLNK(mode):
        raise ContractError(f"locked package member is not a regular file: {name}")
    try:
        return archive.read(info)
    except (OSError, RuntimeError, zipfile.BadZipFile) as exception:
        raise ContractError(
            f"cannot read locked package member {name}: {exception}"
        ) from exception


def artifact_plan(lock: dict[str, object], platform_id: str) -> list[dict[str, str]]:
    platform_lock = require_mapping(require_mapping(lock, "platforms"), platform_id)
    plan: list[dict[str, str]] = []
    for header in EXPECTED_HEADERS:
        plan.append(
            {
                "role": "header",
                "package": "onnxruntime",
                "sourcePath": f"build/native/include/{header}",
                "outputPath": f"include/{header}",
            }
        )
    for package_name in ("onnxruntime", "webgpu"):
        for source_name, output_name in (
            ("LICENSE", f"licenses/{package_name}-LICENSE.txt"),
            ("ThirdPartyNotices.txt", f"licenses/{package_name}-ThirdPartyNotices.txt"),
        ):
            plan.append(
                {
                    "role": "license",
                    "package": package_name,
                    "sourcePath": source_name,
                    "outputPath": output_name,
                }
            )
    runtime_files = platform_lock["runtimeFiles"]
    assert isinstance(runtime_files, list)
    for entry in runtime_files:
        assert isinstance(entry, dict)
        plan.append({key: str(value) for key, value in entry.items()})
    link = validate_artifact_spec(
        platform_lock["linkLibrary"], f"{platform_id}.linkLibrary", role=False
    )
    plan.append(
        {"role": "link-library", **{key: str(value) for key, value in link.items()}}
    )
    unique: dict[str, dict[str, str]] = {}
    for item in plan:
        output = item["outputPath"]
        previous = unique.get(output)
        if previous is not None:
            if (
                previous["package"] != item["package"]
                or previous["sourcePath"] != item["sourcePath"]
            ):
                raise ContractError(f"artifact output collision: {output}")
            continue
        unique[output] = item
    return list(unique.values())


def file_record(path: Path, root: Path, spec: dict[str, str]) -> dict[str, object]:
    data = path.read_bytes()
    return {
        "path": path.relative_to(root).as_posix(),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "role": spec["role"],
        "sourcePackage": spec["package"],
        "sourcePath": spec["sourcePath"],
    }


def artifact_set_digest(records: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for record in sorted(records, key=lambda value: str(value["path"])):
        digest.update(str(record["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(record["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def runtime_identity(lock: dict[str, object], platform_id: str) -> dict[str, object]:
    topology = require_mapping(lock, "topology")
    packages = require_mapping(lock, "packages")
    platform_lock = require_mapping(require_mapping(lock, "platforms"), platform_id)
    return {
        "flavor": "webgpu",
        "kind": "onnxruntime-plugin-webgpu",
        "version": require_string(require_mapping(packages, "onnxruntime"), "version"),
        "abi": "onnxruntime-c-api-24-plugin-ep-0.1",
        "providerName": topology["providerName"],
        "providerVersion": require_string(
            require_mapping(packages, "webgpu"), "version"
        ),
        "registrationName": topology["registrationName"],
        "graphicsBackend": platform_lock["graphicsBackend"],
    }


def stage_runtime(
    lock: dict[str, object],
    platform_id: str,
    package_paths: dict[str, Path],
    output_dir: Path,
) -> Path:
    if output_dir.exists():
        raise ContractError(f"output directory already exists: {output_dir}")
    platform_lock = require_mapping(require_mapping(lock, "platforms"), platform_id)
    plan = artifact_plan(lock, platform_id)
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.", dir=output_dir.parent))
    archives: dict[str, zipfile.ZipFile] = {}
    try:
        for name, path in package_paths.items():
            archives[name] = zipfile.ZipFile(path)
        records: list[dict[str, object]] = []
        for spec in plan:
            data = archive_member(
                archives[spec["package"]], spec["sourcePath"], spec["sourcePath"]
            )
            destination = stage / spec["outputPath"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            records.append(file_record(destination, stage, spec))
        runtime_files = platform_lock["runtimeFiles"]
        assert isinstance(runtime_files, list)
        provider_spec = next(
            entry
            for entry in runtime_files
            if isinstance(entry, dict) and entry.get("role") == "webgpu-plugin"
        )
        link_spec = platform_lock["linkLibrary"]
        assert isinstance(link_spec, dict)
        package_records = []
        packages = require_mapping(lock, "packages")
        for name in ("onnxruntime", "webgpu"):
            package = require_mapping(packages, name)
            package_records.append(
                {
                    "name": name,
                    "id": package["id"],
                    "version": package["version"],
                    "source": package["source"],
                    "catalog": package["catalog"],
                    "bytes": package["bytes"],
                    "sha512": package["sha512"],
                }
            )
        platform_identity = {
            "id": platform_id,
            "operatingSystem": platform_lock["operatingSystem"],
            "architecture": platform_lock["architecture"],
        }
        if "libc" in platform_lock:
            platform_identity["libc"] = platform_lock["libc"]
        manifest = {
            "schemaVersion": 2,
            "contractId": lock["contractId"],
            "platform": platform_identity,
            "runtime": runtime_identity(lock, platform_id),
            "artifacts": {
                "linkLibrary": link_spec["outputPath"],
                "providerLibrary": provider_spec["outputPath"],
                "runtimeFiles": [
                    entry["outputPath"]
                    for entry in runtime_files
                    if isinstance(entry, dict)
                ],
                "files": sorted(records, key=lambda value: str(value["path"])),
                "artifactSetSha256": artifact_set_digest(records),
            },
            "headers": {
                "directory": "include",
                "onnxruntimeVersion": EXPECTED_PACKAGES["onnxruntime"]["version"],
                "files": [record for record in records if record["role"] == "header"],
            },
            "packages": package_records,
            "sessionOptions": copy.deepcopy(lock["sessionOptions"]),
            "qualification": copy.deepcopy(lock["qualification"]),
        }
        manifest_path = stage / "artifact-manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", "utf-8")
        for archive in archives.values():
            archive.close()
        archives.clear()
        os.replace(stage, output_dir)
        return output_dir / "artifact-manifest.json"
    except BaseException:
        for archive in archives.values():
            archive.close()
        shutil.rmtree(stage, ignore_errors=True)
        raise


def validate_sdk(sdk_dir: Path, lock: dict[str, object]) -> dict[str, object]:
    manifest_path = sdk_dir / "artifact-manifest.json"
    if manifest_path.is_symlink() or not manifest_path.is_file():
        raise ContractError("artifact manifest must be a regular file")
    manifest = load_json(manifest_path, "artifact manifest")
    if (
        manifest.get("schemaVersion") != 2
        or manifest.get("contractId") != lock["contractId"]
    ):
        raise ContractError("artifact manifest contract identity is invalid")
    platform_value = require_mapping(manifest, "platform")
    platform_id = require_string(platform_value, "id")
    if platform_id not in require_mapping(lock, "platforms"):
        raise ContractError("artifact manifest platform is invalid")
    platform_lock = require_mapping(require_mapping(lock, "platforms"), platform_id)
    expected_platform = {
        "id": platform_id,
        "operatingSystem": platform_lock["operatingSystem"],
        "architecture": platform_lock["architecture"],
    }
    if "libc" in platform_lock:
        expected_platform["libc"] = platform_lock["libc"]
    if platform_value != expected_platform:
        raise ContractError("artifact manifest platform identity is invalid")
    expected_runtime = runtime_identity(lock, platform_id)
    if manifest.get("runtime") != expected_runtime:
        raise ContractError("artifact manifest runtime identity is invalid")
    if manifest.get("sessionOptions") != lock["sessionOptions"]:
        raise ContractError("artifact manifest session options are invalid")
    if manifest.get("qualification") != lock["qualification"]:
        raise ContractError("artifact manifest qualification state is invalid")
    expected_packages = []
    locked_packages = require_mapping(lock, "packages")
    for name in ("onnxruntime", "webgpu"):
        package = require_mapping(locked_packages, name)
        expected_packages.append(
            {
                "name": name,
                "id": package["id"],
                "version": package["version"],
                "source": package["source"],
                "catalog": package["catalog"],
                "bytes": package["bytes"],
                "sha512": package["sha512"],
            }
        )
    if manifest.get("packages") != expected_packages:
        raise ContractError("artifact manifest package provenance is invalid")
    artifacts = require_mapping(manifest, "artifacts")
    records = artifacts.get("files")
    if not isinstance(records, list):
        raise ContractError("artifact manifest file inventory is invalid")
    plan = artifact_plan(lock, platform_id)
    expected_specs = {item["outputPath"]: item for item in plan}
    seen: set[str] = set()
    for index, record in enumerate(records):
        if not isinstance(record, dict) or set(record) != {
            "path",
            "bytes",
            "sha256",
            "role",
            "sourcePackage",
            "sourcePath",
        }:
            raise ContractError(f"artifact manifest files[{index}] is invalid")
        relative = safe_relative(record.get("path"), f"artifacts.files[{index}].path")
        if relative in seen or relative not in expected_specs:
            raise ContractError(
                "artifact manifest file inventory has an unexpected path"
            )
        seen.add(relative)
        spec = expected_specs[relative]
        if (
            record.get("role") != spec["role"]
            or record.get("sourcePackage") != spec["package"]
            or record.get("sourcePath") != spec["sourcePath"]
            or type(record.get("bytes")) is not int
            or int(record["bytes"]) < 1
            or not isinstance(record.get("sha256"), str)
            or not HEX64.fullmatch(str(record["sha256"]))
        ):
            raise ContractError(f"artifact manifest identity is invalid: {relative}")
        filename = sdk_dir / relative
        if not filename.is_file() or filename.is_symlink():
            raise ContractError(f"SDK artifact is missing or not regular: {relative}")
        if (
            filename.stat().st_size != record["bytes"]
            or file_digest(filename, "sha256") != record["sha256"]
        ):
            raise ContractError(f"SDK artifact hash or byte count mismatch: {relative}")
    if seen != set(expected_specs):
        raise ContractError("artifact manifest file inventory is incomplete")
    actual_inventory = {
        path.relative_to(sdk_dir).as_posix()
        for path in sdk_dir.rglob("*")
        if path.is_file()
        and path.relative_to(sdk_dir).as_posix() != "artifact-manifest.json"
    }
    if actual_inventory != seen or any(
        path.is_symlink() for path in sdk_dir.rglob("*")
    ):
        raise ContractError("SDK contains an undeclared file or symlink")
    if artifacts.get("artifactSetSha256") != artifact_set_digest(records):
        raise ContractError("SDK artifact-set identity is invalid")
    runtime_files = platform_lock["runtimeFiles"]
    assert isinstance(runtime_files, list)
    expected_runtime_files = [
        entry["outputPath"] for entry in runtime_files if isinstance(entry, dict)
    ]
    provider_path = next(
        entry["outputPath"]
        for entry in runtime_files
        if isinstance(entry, dict) and entry.get("role") == "webgpu-plugin"
    )
    link = platform_lock["linkLibrary"]
    assert isinstance(link, dict)
    if (
        artifacts.get("runtimeFiles") != expected_runtime_files
        or artifacts.get("providerLibrary") != provider_path
        or artifacts.get("linkLibrary") != link["outputPath"]
    ):
        raise ContractError("SDK loader artifact paths are invalid")
    expected_header_records = [
        record for record in records if record["role"] == "header"
    ]
    if manifest.get("headers") != {
        "directory": "include",
        "onnxruntimeVersion": EXPECTED_PACKAGES["onnxruntime"]["version"],
        "files": expected_header_records,
    }:
        raise ContractError("SDK header manifest is invalid")
    return manifest


def infer_platform() -> str:
    system = platform.system()
    machine = platform.machine().lower()
    if machine not in {"x86_64", "amd64"}:
        raise ContractError(f"Native WebGPU SDK target requires x86_64, got {machine}")
    if system == "Linux":
        return "linux-x64"
    if system == "Windows":
        return "windows-x64"
    raise ContractError("--platform is required when assembling from a non-target host")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    parser.add_argument("--platform", choices=["linux-x64", "windows-x64"])
    parser.add_argument(
        "--package-cache",
        type=Path,
        default=ROOT / ".cache" / "webgpu-runtime" / "packages",
    )
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--validate-lock", action="store_true")
    parser.add_argument("--validate-sdk", type=Path)
    arguments = parser.parse_args()

    lock = load_lock(arguments.lock)
    if arguments.validate_lock:
        print(arguments.lock.resolve())
        return 0
    if arguments.validate_sdk:
        validate_sdk(arguments.validate_sdk.resolve(), lock)
        print(arguments.validate_sdk.resolve())
        return 0
    platform_id = arguments.platform or infer_platform()
    packages = require_mapping(lock, "packages")
    cache = arguments.package_cache.resolve()
    paths = {
        name: acquire_package(
            require_mapping(packages, name), cache, offline=arguments.offline
        )
        for name in ("onnxruntime", "webgpu")
    }
    output = (
        arguments.output_dir.resolve()
        if arguments.output_dir
        else ROOT / "dist" / "webgpu-runtime" / platform_id
    )
    manifest_path = stage_runtime(lock, platform_id, paths, output)
    validate_sdk(output, lock)
    print(manifest_path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ContractError as exception:
        print(f"error: {exception}", file=sys.stderr)
        raise SystemExit(1)
