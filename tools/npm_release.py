#!/usr/bin/env python3
"""Stage, validate, and pack the light-ocr npm release set."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
import time
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SOURCE_VERSION = json.loads(
    (ROOT / "bindings" / "node" / "package.json").read_text("utf-8")
)["version"]
BUNDLE_ID = "ppocrv6-small-onnx-20260714.2"
MODEL_PACKAGE = "@arcships/light-ocr-model-ppocrv6-small"
FACADE_PACKAGE = "@arcships/light-ocr"
NPM_REGISTRY = "https://registry.npmjs.org/"
REGISTRY_WAIT_SECONDS = 600
PLATFORMS: dict[str, dict[str, Any]] = {
    "macos-arm64": {
        "package": "@arcships/light-ocr-darwin-arm64",
        "os": ["darwin"],
        "cpu": ["arm64"],
        "runtime": "libonnxruntime.1.22.0.dylib",
    },
    "macos-x64": {
        "package": "@arcships/light-ocr-darwin-x64",
        "os": ["darwin"],
        "cpu": ["x64"],
        "runtime": "libonnxruntime.1.22.0.dylib",
    },
    "linux-x64": {
        "package": "@arcships/light-ocr-linux-x64-gnu",
        "os": ["linux"],
        "cpu": ["x64"],
        "libc": ["glibc"],
        "runtime": "libonnxruntime.so.1",
    },
    "windows-x64": {
        "package": "@arcships/light-ocr-win32-x64",
        "os": ["win32"],
        "cpu": ["x64"],
        "runtime": "onnxruntime.dll",
    },
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def read_json(path: Path) -> Any:
    return json.loads(path.read_text("utf-8"))


def remove_and_create(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True)


def copy_file(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise RuntimeError(f"required file is missing: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def reject_symlinks(root: Path) -> None:
    for path in root.rglob("*"):
        if path.is_symlink():
            raise RuntimeError(f"package staging contains a symlink: {path}")


def build_file(build: Path, filename: str) -> Path:
    candidates = [
        build / "bin" / filename,
        build / "bin" / "Release" / filename,
        build / "Release" / filename,
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"build output is missing: {filename}")


def stage_native(arguments: argparse.Namespace) -> None:
    platform = PLATFORMS[arguments.platform_id]
    build = arguments.build_dir.resolve()
    metadata = arguments.metadata_dir.resolve()
    output = arguments.output_dir.resolve()
    remove_and_create(output)

    native = output / "native"
    native.mkdir()
    copy_file(build_file(build, "light_ocr_node.node"), native / "light_ocr_node.node")
    copy_file(build_file(build, platform["runtime"]), native / platform["runtime"])
    copy_file(metadata / "license-inventory.json", output / "license-inventory.json")
    copy_file(metadata / "sbom.spdx.json", output / "sbom.spdx.json")
    shutil.copytree(metadata / "licenses", output / "licenses")

    records = []
    for path in sorted(output.rglob("*")):
        if path.is_file():
            records.append(
                {
                    "path": path.relative_to(output).as_posix(),
                    "bytes": path.stat().st_size,
                    "sha256": sha256(path),
                }
            )
    write_json(
        output / "native-input.json",
        {
            "schemaVersion": "1.0",
            "platformId": arguments.platform_id,
            "package": platform["package"],
            "files": records,
        },
    )
    reject_symlinks(output)
    print(json.dumps({"ok": True, "platformId": arguments.platform_id, "output": str(output)}))


def common_package(name: str, version: str, description: str) -> dict[str, Any]:
    return {
        "name": name,
        "version": version,
        "description": description,
        "license": "Apache-2.0",
        "repository": {
            "type": "git",
            "url": "git+https://github.com/arcships/light-ocr.git",
        },
        "homepage": "https://github.com/arcships/light-ocr#readme",
        "bugs": {"url": "https://github.com/arcships/light-ocr/issues"},
        "publishConfig": {"access": "public", "provenance": True},
    }


def package_readme(name: str, description: str, direct_install: bool) -> str:
    lines = [f"# {name}", "", description, ""]
    if direct_install:
        lines.extend(
            [
                "```bash",
                "npm install @arcships/light-ocr",
                "```",
                "",
                "The default PP-OCRv6 Small model is included as a required dependency; no",
                "runtime model download or postinstall compilation is used.",
            ]
        )
    else:
        lines.extend(
            [
                "This is an internal distribution package for `@arcships/light-ocr`. Install",
                "the facade package instead of depending on this package directly.",
            ]
        )
    lines.extend(
        [
            "",
            "Documentation: https://github.com/arcships/light-ocr",
            "",
            "License: Apache-2.0",
            "",
        ]
    )
    return "\n".join(lines)


def add_project_files(package: Path, readme: str) -> None:
    copy_file(ROOT / "LICENSE", package / "LICENSE")
    copy_file(ROOT / "NOTICE", package / "NOTICE")
    (package / "README.md").write_text(readme, encoding="utf-8")


def copy_tree(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise RuntimeError(f"required directory is missing: {source}")
    for path in source.rglob("*"):
        relative = path.relative_to(source)
        target = destination / relative
        if path.is_symlink():
            raise RuntimeError(f"source directory contains a symlink: {path}")
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif path.is_file():
            copy_file(path, target)


def native_source(root: Path, platform_id: str) -> Path:
    candidates = [root / platform_id, root / f"native-{platform_id}"]
    for candidate in candidates:
        if (candidate / "native-input.json").is_file():
            return candidate
    raise RuntimeError(f"native input is missing for {platform_id}")


def artifact_hashes(package: Path, name: str, version: str) -> None:
    records = []
    for path in sorted(package.rglob("*")):
        if not path.is_file() or path.name == "artifact-hashes.json":
            continue
        records.append(
            {
                "path": path.relative_to(package).as_posix(),
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
        )
    write_json(
        package / "artifact-hashes.json",
        {"schemaVersion": "1.0", "package": name, "version": version, "files": records},
    )


def assemble(arguments: argparse.Namespace) -> None:
    if not re.fullmatch(r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)", arguments.version):
        raise RuntimeError("version must be a plain stable SemVer value")
    version = arguments.version
    if tuple(int(part) for part in version.split(".")) < (0, 2, 0):
        raise RuntimeError("tiled-v1 packages require version 0.2.0 or newer")
    if version != SOURCE_VERSION:
        raise RuntimeError(
            f"release version {version} does not match source version {SOURCE_VERSION}"
        )
    output = arguments.output_dir.resolve()
    native_root = arguments.native_root.resolve()
    bundle = arguments.bundle.resolve()
    remove_and_create(output)

    manifest = read_json(bundle / "manifest.json")
    if manifest.get("bundleId") != BUNDLE_ID:
        raise RuntimeError("model bundle ID does not match the npm release contract")
    normalized_config = read_json(bundle / manifest["normalizedConfigPath"])
    tiled_contract = normalized_config.get("runtimeProfiles", {}).get("tiled", {})
    if (manifest.get("schemaVersion") != "1.0" or
            normalized_config.get("schemaVersion") != "1.2" or
            tiled_contract.get("contractVersion") != "tiled-v1"):
        raise RuntimeError("model bundle does not contain the tiled-v1 release contract")

    facade = output / "facade"
    facade.mkdir()
    copy_tree(ROOT / "bindings" / "node" / "js", facade / "js")
    facade_json = common_package(
        FACADE_PACKAGE,
        version,
        "Offline PP-OCRv6 OCR for Node.js, powered by an embeddable C++ core",
    )
    facade_json.update(
        {
            "keywords": ["ocr", "offline-ocr", "pp-ocrv6", "paddleocr", "node-api", "napi"],
            "type": "commonjs",
            "main": "./js/index.cjs",
            "module": "./js/index.mjs",
            "types": "./js/index.d.ts",
            "exports": {
                ".": {
                    "types": "./js/index.d.ts",
                    "import": "./js/index.mjs",
                    "require": "./js/index.cjs",
                }
            },
            "files": ["js/", "README.md", "LICENSE", "NOTICE"],
            "engines": {"node": "^22.0.0 || ^24.0.0"},
            "dependencies": {MODEL_PACKAGE: version},
            "optionalDependencies": {
                PLATFORMS[key]["package"]: version for key in sorted(PLATFORMS)
            },
        }
    )
    write_json(facade / "package.json", facade_json)
    add_project_files(
        facade,
        package_readme(
            FACADE_PACKAGE,
            "Offline OCR for Node.js applications, powered by PP-OCRv6 Small.",
            True,
        ),
    )

    model = output / "model-ppocrv6-small"
    model.mkdir()
    copy_tree(bundle, model / "bundle")
    model_json = common_package(
        MODEL_PACKAGE,
        version,
        "Pinned PP-OCRv6 Small model bundle for @arcships/light-ocr",
    )
    model_json.update(
        {
            "files": ["bundle/", "README.md", "LICENSE", "NOTICE"],
            "exports": {"./bundle/manifest.json": "./bundle/manifest.json"},
            "lightOcr": {
                "bundleId": BUNDLE_ID,
                "manifestSchemaVersion": manifest["schemaVersion"],
                "normalizedConfigSchemaVersion": normalized_config["schemaVersion"],
                "tiledContractVersion": tiled_contract["contractVersion"],
                "paddleOcrRevision": "b03f46425e8ff4442b268ce449e3eef758146cd4",
            },
        }
    )
    write_json(model / "package.json", model_json)
    add_project_files(
        model,
        package_readme(
            MODEL_PACKAGE,
            "The pinned PP-OCRv6 Small model bundle for `@arcships/light-ocr`.",
            False,
        ),
    )

    for platform_id, platform in PLATFORMS.items():
        source = native_source(native_root, platform_id)
        descriptor = read_json(source / "native-input.json")
        if descriptor.get("platformId") != platform_id:
            raise RuntimeError(f"native input identity mismatch for {platform_id}")
        package = output / platform_id
        package.mkdir()
        copy_tree(source / "native", package / "native")
        copy_tree(source / "licenses", package / "licenses")
        copy_file(source / "license-inventory.json", package / "license-inventory.json")
        copy_file(source / "sbom.spdx.json", package / "sbom.spdx.json")
        package_json = common_package(
            platform["package"],
            version,
            f"Native {platform_id} runtime for @arcships/light-ocr",
        )
        package_json.update(
            {
                "main": "./native/light_ocr_node.node",
                "exports": {".": "./native/light_ocr_node.node"},
                "os": platform["os"],
                "cpu": platform["cpu"],
                "files": [
                    "native/",
                    "licenses/",
                    "license-inventory.json",
                    "sbom.spdx.json",
                    "artifact-hashes.json",
                    "README.md",
                    "LICENSE",
                    "NOTICE",
                ],
                "engines": {"node": "^22.0.0 || ^24.0.0"},
            }
        )
        if "libc" in platform:
            package_json["libc"] = platform["libc"]
        write_json(package / "package.json", package_json)
        add_project_files(
            package,
            package_readme(
                platform["package"],
                f"The prebuilt {platform_id} native runtime for `@arcships/light-ocr`.",
                False,
            ),
        )
        artifact_hashes(package, platform["package"], version)

    for package in output.iterdir():
        if package.is_dir():
            reject_symlinks(package)
    print(json.dumps({"ok": True, "version": version, "output": str(output)}))


def git_revision() -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True, capture_output=True, text=True
    )
    return completed.stdout.strip()


def package_directories(staging: Path) -> list[Path]:
    packages = sorted(path for path in staging.iterdir() if (path / "package.json").is_file())
    if len(packages) != 6:
        raise RuntimeError(f"expected six staged packages, found {len(packages)}")
    names = [read_json(path / "package.json")["name"] for path in packages]
    if len(set(names)) != 6:
        raise RuntimeError("staged package names are not unique")
    return packages


def run_npm_pack(npm: str, package: Path, destination: Path) -> dict[str, Any]:
    completed = subprocess.run(
        [npm, "pack", "--json", "--ignore-scripts", "--pack-destination", str(destination), str(package)],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    records = json.loads(completed.stdout)
    if len(records) != 1:
        raise RuntimeError(f"npm pack returned an unexpected result for {package}")
    return records[0]


def validate_tarball(package: Path, tarball: Path, npm_record: dict[str, Any]) -> None:
    expected = {
        path.relative_to(package).as_posix()
        for path in package.rglob("*")
        if path.is_file()
    }
    reported = {record["path"] for record in npm_record["files"]}
    if reported != expected:
        missing = sorted(expected - reported)
        extra = sorted(reported - expected)
        raise RuntimeError(f"npm inventory mismatch for {package.name}: missing={missing}, extra={extra}")

    archived: set[str] = set()
    with tarfile.open(tarball, "r:gz") as archive:
        for member in archive.getmembers():
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts or not path.parts or path.parts[0] != "package":
                raise RuntimeError(f"unsafe npm tar entry: {member.name}")
            if member.issym() or member.islnk():
                raise RuntimeError(f"npm tar contains a link: {member.name}")
            if member.isfile():
                archived.add(PurePosixPath(*path.parts[1:]).as_posix())
    if archived != expected:
        raise RuntimeError(f"tar inventory differs from staging for {package.name}")


def pack(arguments: argparse.Namespace) -> None:
    staging = arguments.staging_dir.resolve()
    output = arguments.output_dir.resolve()
    remove_and_create(output)
    release_records = []
    with tempfile.TemporaryDirectory(prefix="light-ocr-npm-pack-") as temporary:
        first = Path(temporary) / "first"
        second = Path(temporary) / "second"
        first.mkdir()
        second.mkdir()
        for package in package_directories(staging):
            reject_symlinks(package)
            record = run_npm_pack(arguments.npm, package, first)
            repeat = run_npm_pack(arguments.npm, package, second)
            first_tarball = first / record["filename"]
            second_tarball = second / repeat["filename"]
            first_sha256 = sha256(first_tarball)
            if first_sha256 != sha256(second_tarball):
                raise RuntimeError(f"npm pack is not deterministic for {record['name']}")
            validate_tarball(package, first_tarball, record)
            destination = output / record["filename"]
            shutil.copyfile(first_tarball, destination)
            release_records.append(
                {
                    "name": record["name"],
                    "version": record["version"],
                    "filename": record["filename"],
                    "bytes": destination.stat().st_size,
                    "unpackedBytes": record["unpackedSize"],
                    "sha256": first_sha256,
                    "shasum": record["shasum"],
                    "integrity": record["integrity"],
                    "fileCount": len(record["files"]),
                }
            )
    versions = {record["version"] for record in release_records}
    if len(versions) != 1:
        raise RuntimeError("npm packages do not share one lockstep version")
    release_manifest = {
        "schemaVersion": "1.0",
        "gitRevision": git_revision(),
        "version": next(iter(versions)),
        "distTag": "next",
        "npmVersion": subprocess.run(
            [arguments.npm, "--version"],
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        ).stdout.strip(),
        "packages": sorted(release_records, key=lambda record: record["name"]),
    }
    write_json(output / "release-manifest.json", release_manifest)
    print(json.dumps({"ok": True, "release": release_manifest}, separators=(",", ":")))


def npm_integrity(npm: str, specification: str) -> str | None:
    completed = subprocess.run(
        [
            npm,
            "view",
            specification,
            "dist.integrity",
            "--json",
            "--prefer-online",
            f"--registry={NPM_REGISTRY}",
        ],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if completed.returncode == 0:
        value = json.loads(completed.stdout)
        if not isinstance(value, str) or not value.startswith("sha512-"):
            raise RuntimeError(f"registry returned invalid integrity for {specification}")
        return value
    if "E404" in completed.stderr or "404 Not Found" in completed.stderr:
        return None
    raise RuntimeError(f"npm view failed for {specification}: {completed.stderr.strip()}")


def wait_for_integrity(npm: str, specification: str, expected: str) -> None:
    attempts = REGISTRY_WAIT_SECONDS // 3
    for _ in range(attempts):
        actual = npm_integrity(npm, specification)
        if actual == expected:
            return
        if actual is not None and actual != expected:
            raise RuntimeError(f"registry integrity mismatch for {specification}")
        time.sleep(3)
    raise RuntimeError(
        f"registry did not expose {specification} within {REGISTRY_WAIT_SECONDS} seconds"
    )


def npm_dist_tag(npm: str, package: str, tag: str) -> str | None:
    completed = subprocess.run(
        [
            npm,
            "view",
            package,
            f"dist-tags.{tag}",
            "--json",
            "--prefer-online",
            f"--registry={NPM_REGISTRY}",
        ],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"npm dist-tag lookup failed for {package}: {completed.stderr.strip()}"
        )
    value = json.loads(completed.stdout)
    if value is not None and not isinstance(value, str):
        raise RuntimeError(f"registry returned invalid {tag} tag for {package}")
    return value


def wait_for_dist_tag(npm: str, package: str, tag: str, version: str) -> None:
    attempts = REGISTRY_WAIT_SECONDS // 3
    for _ in range(attempts):
        if npm_dist_tag(npm, package, tag) == version:
            return
        time.sleep(3)
    raise RuntimeError(
        f"registry did not expose {package}@{version} as {tag} "
        f"within {REGISTRY_WAIT_SECONDS} seconds"
    )


def publish(arguments: argparse.Namespace) -> None:
    tarballs = arguments.tarball_dir.resolve()
    release = read_json(tarballs / "release-manifest.json")
    records = {record["name"]: record for record in release["packages"]}
    if arguments.phase == "dependencies":
        names = [MODEL_PACKAGE] + sorted(
            platform["package"] for platform in PLATFORMS.values()
        )
    else:
        names = [FACADE_PACKAGE]
    for name in names:
        record = records[name]
        specification = f"{name}@{record['version']}"
        existing = npm_integrity(arguments.npm, specification)
        if existing is not None:
            if existing != record["integrity"]:
                raise RuntimeError(f"published package integrity mismatch for {specification}")
            print(json.dumps({"package": specification, "status": "already-published"}))
            continue
        tarball = tarballs / record["filename"]
        if sha256(tarball) != record["sha256"]:
            raise RuntimeError(f"release tarball hash mismatch for {specification}")
        subprocess.run(
            [
                arguments.npm,
                "publish",
                str(tarball),
                "--access",
                "public",
                "--tag",
                arguments.tag,
                "--provenance",
            ],
            cwd=ROOT,
            check=True,
        )
        wait_for_integrity(arguments.npm, specification, record["integrity"])
        print(json.dumps({"package": specification, "status": "published"}))


def promote(arguments: argparse.Namespace) -> None:
    tarballs = arguments.tarball_dir.resolve()
    release = read_json(tarballs / "release-manifest.json")
    if (
        arguments.expected_version
        and release.get("version") != arguments.expected_version
    ):
        raise RuntimeError("release manifest version does not match promotion request")
    records = {record["name"]: record for record in release["packages"]}
    names = [MODEL_PACKAGE] + sorted(
        platform["package"] for platform in PLATFORMS.values()
    ) + [FACADE_PACKAGE]
    for name in names:
        record = records[name]
        specification = f"{record['name']}@{record['version']}"
        wait_for_integrity(arguments.npm, specification, record["integrity"])
        subprocess.run(
            [arguments.npm, "dist-tag", "add", specification, arguments.tag],
            cwd=ROOT,
            check=True,
        )
        wait_for_dist_tag(
            arguments.npm, record["name"], arguments.tag, record["version"]
        )
        print(json.dumps({"package": specification, "tag": arguments.tag}))


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    native = subparsers.add_parser("stage-native")
    native.add_argument("--platform-id", choices=sorted(PLATFORMS), required=True)
    native.add_argument("--build-dir", type=Path, required=True)
    native.add_argument("--metadata-dir", type=Path, required=True)
    native.add_argument("--output-dir", type=Path, required=True)
    native.set_defaults(handler=stage_native)

    assembly = subparsers.add_parser("assemble")
    assembly.add_argument("--version", required=True)
    assembly.add_argument("--bundle", type=Path, required=True)
    assembly.add_argument("--native-root", type=Path, required=True)
    assembly.add_argument("--output-dir", type=Path, required=True)
    assembly.set_defaults(handler=assemble)

    packing = subparsers.add_parser("pack")
    packing.add_argument("--staging-dir", type=Path, required=True)
    packing.add_argument("--output-dir", type=Path, required=True)
    packing.add_argument("--npm", default="npm")
    packing.set_defaults(handler=pack)

    publishing = subparsers.add_parser("publish")
    publishing.add_argument("--tarball-dir", type=Path, required=True)
    publishing.add_argument("--phase", choices=["dependencies", "facade"], required=True)
    publishing.add_argument("--tag", default="next")
    publishing.add_argument("--npm", default="npm")
    publishing.set_defaults(handler=publish)

    promotion = subparsers.add_parser("promote")
    promotion.add_argument("--tarball-dir", type=Path, required=True)
    promotion.add_argument("--tag", default="latest")
    promotion.add_argument("--expected-version")
    promotion.add_argument("--npm", default="npm")
    promotion.set_defaults(handler=promote)

    arguments = parser.parse_args()
    arguments.handler(arguments)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
