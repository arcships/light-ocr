#!/usr/bin/env python3
"""Generate a build manifest, SPDX SBOM, and copied license inventory."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
from typing import Any

try:
    from tools.webgpu import build_runtime as webgpu_runtime
except ModuleNotFoundError:  # Direct execution sets sys.path to tools/.
    from webgpu import build_runtime as webgpu_runtime


ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command(arguments: list[str]) -> str:
    try:
        completed = subprocess.run(
            arguments, check=True, capture_output=True, text=True, encoding="utf-8"
        )
        return completed.stdout.strip() or completed.stderr.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unavailable"


def tool_version(executable: str) -> str:
    if not executable or executable == "unavailable":
        return "unavailable"
    name = Path(executable).name.lower()
    if name in {"cl", "cl.exe", "link", "link.exe"}:
        arguments = [executable, "/?"]
    elif name in {"ld", "ld.exe"}:
        arguments = [executable, "-v"]
    else:
        arguments = [executable, "--version"]
    try:
        completed = subprocess.run(
            arguments,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        output = completed.stdout.strip() or completed.stderr.strip()
        return output.splitlines()[0] if output else "unavailable"
    except OSError:
        return "unavailable"


def cache_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text("utf-8", errors="replace").splitlines():
        if (
            not line
            or line.startswith(("#", "//"))
            or "=" not in line
            or ":" not in line
        ):
            continue
        key_type, value = line.split("=", 1)
        key = key_type.split(":", 1)[0]
        values[key] = value
    return values


def source_snapshot() -> dict[str, Any]:
    excluded_roots = {".cache", ".git", "build", "dist", "models/generated", "reports"}
    files: list[Path] = []
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(ROOT).as_posix()
        if any(
            relative == root or relative.startswith(root + "/")
            for root in excluded_roots
        ):
            continue
        if any(
            part.startswith("build-") or part == "__pycache__"
            for part in path.relative_to(ROOT).parts
        ):
            continue
        if path.name == ".DS_Store":
            continue
        files.append(path)
    digest = hashlib.sha256()
    for path in sorted(files, key=lambda item: item.relative_to(ROOT).as_posix()):
        relative = path.relative_to(ROOT).as_posix()
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(sha256(path).encode("ascii"))
        digest.update(b"\n")
    return {"sha256": digest.hexdigest(), "fileCount": len(files)}


def compile_commands(build: Path) -> dict[str, Any]:
    path = build / "compile_commands.json"
    if not path.is_file():
        return {"available": False}
    records = json.loads(path.read_text("utf-8"))
    project_records = []
    source_root = str(ROOT / "src") + os.sep
    for record in records:
        source = os.path.realpath(record["file"])
        if not source.startswith(source_root):
            continue
        command_text = record.get("command", " ".join(record.get("arguments", [])))
        command_text = command_text.replace(str(build), "${BUILD_DIR}").replace(
            str(ROOT), "${SOURCE_DIR}"
        )
        project_records.append(
            {
                "file": str(Path(source).relative_to(ROOT)),
                "command": command_text,
            }
        )
    project_records.sort(key=lambda record: record["file"])
    normalized = json.dumps(
        project_records, sort_keys=True, separators=(",", ":")
    ).encode()
    return {
        "available": True,
        "sha256": hashlib.sha256(normalized).hexdigest(),
        "project": project_records,
    }


def source_dir(build: Path, name: str) -> Path:
    candidates = [
        build / "_deps" / f"{name}-src",
        ROOT / "build" / "_deps" / f"{name}-src",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise RuntimeError(f"dependency source directory not found: {name}-src")


def copy_licenses(
    build: Path, output: Path, include_stb: bool, webgpu_sdk: Path | None
) -> list[dict[str, str]]:
    license_dir = output / "licenses"
    license_dir.mkdir(parents=True, exist_ok=True)
    sources = [
        (source_dir(build, "opencv") / "LICENSE", "opencv-Apache-2.0.txt", "opencv"),
        (source_dir(build, "opencv") / "COPYRIGHT", "opencv-COPYRIGHT.txt", "opencv"),
        (
            source_dir(build, "opencv") / "3rdparty" / "zlib" / "LICENSE",
            "opencv-zlib.txt",
            "zlib",
        ),
        (source_dir(build, "clipper") / "LICENSE", "clipper-BSL-1.0.txt", "clipper"),
        (
            source_dir(build, "nlohmann_json") / "LICENSE.MIT",
            "nlohmann-json-MIT.txt",
            "nlohmann-json",
        ),
    ]
    if webgpu_sdk is None:
        sources.extend(
            [
                (
                    source_dir(build, "onnxruntime_package") / "LICENSE",
                    "onnxruntime-MIT.txt",
                    "onnxruntime",
                ),
                (
                    source_dir(build, "onnxruntime_package") / "ThirdPartyNotices.txt",
                    "onnxruntime-ThirdPartyNotices.txt",
                    "onnxruntime",
                ),
            ]
        )
    else:
        sources.extend(
            [
                (
                    webgpu_sdk / "licenses" / "onnxruntime-LICENSE.txt",
                    "onnxruntime-MIT.txt",
                    "onnxruntime",
                ),
                (
                    webgpu_sdk / "licenses" / "onnxruntime-ThirdPartyNotices.txt",
                    "onnxruntime-ThirdPartyNotices.txt",
                    "onnxruntime",
                ),
                (
                    webgpu_sdk / "licenses" / "webgpu-LICENSE.txt",
                    "onnxruntime-webgpu-MIT.txt",
                    "onnxruntime-webgpu",
                ),
                (
                    webgpu_sdk / "licenses" / "webgpu-ThirdPartyNotices.txt",
                    "onnxruntime-webgpu-ThirdPartyNotices.txt",
                    "onnxruntime-webgpu",
                ),
            ]
        )
    if include_stb:
        sources.append(
            (source_dir(build, "stb") / "LICENSE", "stb-MIT-or-Unlicense.txt", "stb")
        )
    bundle = ROOT / "models" / "generated" / "ppocrv6-small-onnx-20260714.2"
    sources.extend(
        [
            (
                bundle / "LICENSES" / "PaddleOCR-Apache-2.0.txt",
                "PP-OCRv6-Apache-2.0.txt",
                "PP-OCRv6-models",
            ),
            (
                bundle / "LICENSES" / "MODEL-NOTICE.md",
                "PP-OCRv6-MODEL-NOTICE.md",
                "PP-OCRv6-models",
            ),
        ]
    )
    inventory: list[dict[str, str]] = []
    for source, filename, component in sources:
        destination = license_dir / filename
        shutil.copyfile(source, destination)
        inventory.append(
            {
                "component": component,
                "file": f"licenses/{filename}",
                "sha256": sha256(destination),
            }
        )

    carotene_source = (
        source_dir(build, "opencv") / "3rdparty" / "carotene" / "src" / "common.hpp"
    )
    text = carotene_source.read_text("utf-8")
    end = text.find("*/")
    if end < 0:
        raise RuntimeError("cannot extract Carotene license block")
    destination = license_dir / "opencv-carotene-BSD-3-Clause.txt"
    destination.write_text(text[: end + 2] + "\n", encoding="utf-8")
    inventory.append(
        {
            "component": "carotene",
            "file": f"licenses/{destination.name}",
            "sha256": sha256(destination),
        }
    )
    return inventory


def spdx_package(identifier: str, record: dict[str, Any]) -> dict[str, Any]:
    return {
        "name": record["name"],
        "SPDXID": identifier,
        "versionInfo": record["version"],
        "downloadLocation": record["source"],
        "filesAnalyzed": False,
        "checksums": [{"algorithm": "SHA256", "checksumValue": record["sha256"]}],
        "licenseConcluded": record["license"],
        "licenseDeclared": record["license"],
        "copyrightText": "NOASSERTION",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--platform-id", required=True)
    parser.add_argument(
        "--configuration",
        default="",
        help="built configuration for multi-config generators, for example Release",
    )
    arguments = parser.parse_args()
    build = arguments.build_dir.resolve()
    output = arguments.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cache = cache_values(build / "CMakeCache.txt")
    runtime_flavor = cache.get("LIGHT_OCR_ONNXRUNTIME_FLAVOR", "cpu")
    webgpu_sdk = (
        Path(cache["LIGHT_OCR_WEBGPU_SDK_DIR"]).resolve()
        if runtime_flavor == "webgpu" and cache.get("LIGHT_OCR_WEBGPU_SDK_DIR")
        else None
    )
    webgpu_manifest = None
    if runtime_flavor == "webgpu":
        if webgpu_sdk is None:
            raise RuntimeError("WebGPU build metadata has no SDK directory")
        webgpu_manifest = webgpu_runtime.validate_sdk(
            webgpu_sdk, webgpu_runtime.load_lock()
        )
    include_stb = (
        cache.get("LIGHT_OCR_BUILD_NODE") == "ON"
        or cache.get("LIGHT_OCR_BUILD_FUZZERS") == "ON"
    )
    dependency_lock_path = ROOT / "models" / "deps.lock.json"
    bundle_lock_path = ROOT / "models" / "bundles.lock.json"
    dependency_lock = json.loads(dependency_lock_path.read_text("utf-8"))
    bundle_lock = json.loads(bundle_lock_path.read_text("utf-8"))
    revision = command(["git", "rev-parse", "HEAD"])

    artifact_candidates = []
    for path in build.rglob("*"):
        if not path.is_file() or "CMakeFiles" in path.parts or "_deps" in path.parts:
            continue
        if (
            path.name.startswith("light_ocr_")
            or "light_ocr_core" in path.name
            or "onnxruntime" in path.name
            or (
                runtime_flavor == "webgpu"
                and path.name in {"dxcompiler.dll", "dxil.dll"}
            )
        ):
            artifact_candidates.append(path)
    artifacts = [
        {
            "path": str(path.relative_to(build)),
            "bytes": path.stat().st_size,
            "sha256": sha256(path),
        }
        for path in sorted(artifact_candidates)
        if path.is_file()
    ]
    if not artifacts:
        raise RuntimeError("no light-ocr build artifacts found")
    compiler = cache.get("CMAKE_CXX_COMPILER", "unavailable")
    compiler_target = cache.get("CMAKE_CXX_COMPILER_TARGET", "")
    if not compiler_target and compiler != "unavailable":
        compiler_target = cache.get("CMAKE_GENERATOR_PLATFORM", "")
        if not compiler_target and Path(compiler).name.lower() not in {"cl", "cl.exe"}:
            compiler_target = command([compiler, "-dumpmachine"])
        if compiler_target == "unavailable" or not compiler_target:
            compiler_target = cache.get("CMAKE_SYSTEM_PROCESSOR", platform.machine())
    linker = cache.get("CMAKE_LINKER", "")
    sdk_path = cache.get("CMAKE_OSX_SYSROOT", cache.get("CMAKE_SYSROOT", ""))
    sdk_version = ""
    if platform.system() == "Darwin":
        if not sdk_path:
            sdk_path = command(["xcrun", "--show-sdk-path"])
        sdk_version = command(["xcrun", "--show-sdk-version"])
    build_type = arguments.configuration or cache.get("CMAKE_BUILD_TYPE", "unavailable")
    manifest = {
        "schemaVersion": "1.0",
        "platformId": arguments.platform_id,
        "source": {"gitRevision": revision, "snapshot": source_snapshot()},
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
        },
        "toolchain": {
            "cmake": command(["cmake", "--version"]).splitlines()[0],
            "compilerPath": compiler,
            "compiler": tool_version(compiler),
            "compilerTarget": compiler_target or "unavailable",
            "linkerPath": linker or "unavailable",
            "linker": tool_version(linker),
            "generator": cache.get("CMAKE_GENERATOR", "unavailable"),
            "buildType": build_type,
            "systemName": cache.get("CMAKE_SYSTEM_NAME", platform.system()),
            "systemProcessor": cache.get("CMAKE_SYSTEM_PROCESSOR", platform.machine()),
            "sysroot": sdk_path,
            "sdkVersion": sdk_version,
            "deploymentTarget": cache.get(
                "CMAKE_OSX_DEPLOYMENT_TARGET",
                cache.get("CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION", ""),
            ),
            "cxxFlags": {
                "common": cache.get("CMAKE_CXX_FLAGS", ""),
                "configuration": cache.get(f"CMAKE_CXX_FLAGS_{build_type.upper()}", ""),
            },
            "linkerFlags": {
                "common": cache.get("CMAKE_EXE_LINKER_FLAGS", ""),
                "configuration": cache.get(
                    f"CMAKE_EXE_LINKER_FLAGS_{build_type.upper()}", ""
                ),
            },
        },
        "compileCommands": compile_commands(build),
        "locks": {
            "dependenciesSha256": sha256(dependency_lock_path),
            "bundlesSha256": sha256(bundle_lock_path),
            **(
                {
                    "webgpuContractId": webgpu_manifest["contractId"],
                    "webgpuArtifactSetSha256": webgpu_manifest["artifacts"][
                        "artifactSetSha256"
                    ],
                    "webgpuManifestSha256": sha256(
                        webgpu_sdk / "artifact-manifest.json"
                    ),
                }
                if webgpu_manifest is not None and webgpu_sdk is not None
                else {}
            ),
        },
        "artifacts": artifacts,
    }
    archive_record = bundle_lock["bundles"][0]["bundleArchive"]
    archive_path = ROOT / "models" / "generated" / archive_record["filename"]
    if (
        not archive_path.is_file()
        or archive_path.stat().st_size != archive_record["bytes"]
        or sha256(archive_path) != archive_record["sha256"]
    ):
        raise RuntimeError(
            "locked model bundle archive is missing or has the wrong identity"
        )
    manifest["modelBundleArchive"] = archive_record
    (output / "build-manifest.json").write_text(
        json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )

    inventory = copy_licenses(build, output, include_stb, webgpu_sdk)
    (output / "license-inventory.json").write_text(
        json.dumps(
            {"schemaVersion": "1.0", "files": inventory},
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n",
        encoding="utf-8",
    )

    namespace_seed = hashlib.sha256(
        f"{arguments.platform_id}:{revision}:{sha256(dependency_lock_path)}".encode()
    ).hexdigest()
    packages = [
        {
            "name": "light-ocr-core",
            "SPDXID": "SPDXRef-Package-light-ocr-core",
            "versionInfo": "0.1.0",
            "downloadLocation": "NOASSERTION",
            "filesAnalyzed": False,
            "licenseConcluded": "Apache-2.0",
            "licenseDeclared": "Apache-2.0",
            "copyrightText": "Copyright 2026 light-ocr contributors",
        }
    ]
    relationships: list[dict[str, str]] = []
    for record in dependency_lock["dependencies"]:
        if runtime_flavor == "webgpu" and record.get("runtimeFlavor") == "cpu":
            continue
        if record["name"] == "stb" and not include_stb:
            continue
        identifier = "SPDXRef-Package-" + record["name"].replace("_", "-")
        packages.append(spdx_package(identifier, record))
        relationships.append(
            {
                "spdxElementId": "SPDXRef-Package-light-ocr-core",
                "relationshipType": "DEPENDS_ON",
                "relatedSpdxElement": identifier,
            }
        )
        for component in record.get("buildOptions", {}).get("bundledComponents", []):
            component_id = "SPDXRef-Package-opencv-" + component["name"]
            packages.append(
                {
                    "name": component["name"],
                    "SPDXID": component_id,
                    "versionInfo": component["version"],
                    "downloadLocation": record["source"],
                    "filesAnalyzed": False,
                    "licenseConcluded": component["license"],
                    "licenseDeclared": component["license"],
                    "copyrightText": "NOASSERTION",
                }
            )
            relationships.append(
                {
                    "spdxElementId": identifier,
                    "relationshipType": "CONTAINS",
                    "relatedSpdxElement": component_id,
                }
            )

    if webgpu_manifest is not None:
        for record in webgpu_manifest["packages"]:
            identifier = "SPDXRef-Package-" + record["name"].replace("_", "-")
            packages.append(
                {
                    "name": record["id"],
                    "SPDXID": identifier,
                    "versionInfo": record["version"],
                    "downloadLocation": record["source"],
                    "filesAnalyzed": False,
                    "checksums": [
                        {"algorithm": "SHA512", "checksumValue": record["sha512"]}
                    ],
                    "licenseConcluded": "MIT",
                    "licenseDeclared": "MIT",
                    "copyrightText": "NOASSERTION",
                }
            )
            relationships.append(
                {
                    "spdxElementId": "SPDXRef-Package-light-ocr-core",
                    "relationshipType": "DEPENDS_ON",
                    "relatedSpdxElement": identifier,
                }
            )

    bundle_record = bundle_lock["bundles"][0]
    model_id = "SPDXRef-Package-PP-OCRv6-small-models"
    packages.append(
        {
            "name": "PP-OCRv6-small-ONNX-models",
            "SPDXID": model_id,
            "versionInfo": bundle_record["bundleId"],
            "downloadLocation": bundle_record["artifacts"][0]["url"],
            "filesAnalyzed": False,
            "checksums": [
                {
                    "algorithm": "SHA256",
                    "checksumValue": bundle_record["bundleArchive"]["sha256"],
                }
            ],
            "licenseConcluded": bundle_record["license"]["spdx"],
            "licenseDeclared": "Apache-2.0",
            "copyrightText": "NOASSERTION",
        }
    )
    relationships.append(
        {
            "spdxElementId": "SPDXRef-Package-light-ocr-core",
            "relationshipType": "DEPENDS_ON",
            "relatedSpdxElement": model_id,
        }
    )
    revision_time = command(["git", "show", "-s", "--format=%cI", revision])
    if revision_time == "unavailable":
        raise RuntimeError("cannot determine the release commit timestamp")
    created = (
        datetime.fromisoformat(revision_time.replace("Z", "+00:00"))
        .astimezone(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )
    sbom = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"light-ocr-core-{arguments.platform_id}",
        "documentNamespace": f"https://light-ocr.invalid/spdx/{namespace_seed}",
        "creationInfo": {
            "created": created,
            "creators": ["Tool: light-ocr-generate-release-metadata/1.0"],
        },
        "documentDescribes": ["SPDXRef-Package-light-ocr-core"],
        "packages": packages,
        "relationships": relationships,
    }
    (output / "sbom.spdx.json").write_text(
        json.dumps(sbom, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8"
    )
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
