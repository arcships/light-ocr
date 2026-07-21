from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform as host_platform

from tests.python.webgpu_runtime_fixtures import create_fake_packages
from tools import npm_release


def release_metadata(root: Path) -> Path:
    metadata = root / "metadata"
    (metadata / "licenses").mkdir(parents=True)
    (metadata / "licenses" / "notice.txt").write_text("notice\n", "utf-8")
    (metadata / "license-inventory.json").write_text(
        '{"schemaVersion":"1.0","files":[]}\n', "utf-8"
    )
    (metadata / "sbom.spdx.json").write_text(
        '{"spdxVersion":"SPDX-2.3"}\n', "utf-8"
    )
    return metadata


def stage_cpu_native_packages(root: Path) -> Path:
    build_dir = root / "build"
    binaries = build_dir / "bin"
    binaries.mkdir(parents=True)
    (binaries / "light_ocr_node.node").write_bytes(b"native-addon")
    for platform in npm_release.PLATFORMS.values():
        (binaries / platform["runtime"]).write_bytes(platform["runtime"].encode())

    metadata = release_metadata(root)
    native_root = root / "native"
    for platform_id in npm_release.PLATFORMS:
        npm_release.stage_native(
            argparse.Namespace(
                platform_id=platform_id,
                build_dir=build_dir,
                metadata_dir=metadata,
                output_dir=native_root / platform_id,
            )
        )
    return native_root


def model_bundle(root: Path) -> Path:
    bundle = root / "bundle"
    bundle.mkdir()
    (bundle / "manifest.json").write_text(
        json.dumps(
            {
                "schemaVersion": "1.2",
                "bundleId": npm_release.BUNDLE_ID,
                "normalizedConfigPath": "normalized-config.json",
                "providers": {
                    "apple": {
                        "schemaVersion": "1.1",
                        "devicePolicy": "open-macos",
                        "architectures": ["arm64", "x86_64"],
                        "validatedDeviceFamilies": ["Apple M4"],
                    },
                    "webgpu": {
                        "schemaVersion": "1.0",
                        "conversionId": "onnxruntime-float16-1.24.4-20260719.1",
                        "precision": "fp16",
                        "graphOptimizationLevel": "extended",
                        "cpuPartition": "allow-required",
                        "requiredCpuOperators": ["Concat", "Gather", "Slice"],
                    },
                },
            }
        )
        + "\n",
        "utf-8",
    )
    (bundle / "normalized-config.json").write_text(
        json.dumps(
            {
                "schemaVersion": "1.2",
                "runtimeProfiles": {"tiled": {"contractVersion": "tiled-v1"}},
            }
        )
        + "\n",
        "utf-8",
    )
    return bundle


def webgpu_stage_inputs(
    root: Path, lock: dict[str, object]
) -> tuple[dict[str, Path], Path, Path]:
    packages = create_fake_packages(root, lock)
    build_dir = root / "build"
    binaries = build_dir / "bin"
    binaries.mkdir(parents=True)
    (binaries / "light_ocr_node.node").write_bytes(b"addon")
    return packages, build_dir, release_metadata(root)


def current_platform_id() -> str | None:
    machine = host_platform.machine().lower()
    system = host_platform.system()
    if system == "Darwin" and machine in {"arm64", "aarch64"}:
        return "macos-arm64"
    if system == "Darwin" and machine in {"x86_64", "amd64"}:
        return "macos-x64"
    if system == "Linux" and machine in {"x86_64", "amd64"}:
        return "linux-x64"
    if system == "Linux" and machine in {"arm64", "aarch64"}:
        return "linux-arm64"
    if system == "Windows" and machine in {"x86_64", "amd64"}:
        return "windows-x64"
    if system == "Windows" and machine in {"arm64", "aarch64"}:
        return "windows-arm64"
    return None
