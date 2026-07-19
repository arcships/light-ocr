from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "webgpu" / "build_runtime.py"
SPEC = importlib.util.spec_from_file_location("webgpu_build_runtime", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
build_runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build_runtime)


def locked() -> dict[str, object]:
    return json.loads(
        (ROOT / "tools" / "webgpu" / "runtime-lock.json").read_text("utf-8")
    )


def pending_locked() -> dict[str, object]:
    lock = locked()
    qualification = lock["qualification"]
    qualification["status"] = "development-pending-device-validation"
    qualification["providerGatePassed"] = False
    qualification["productionArtifactQualified"] = False
    qualification["qualifiedArtifactSetSha256"] = {
        "linux-x64": None,
        "windows-x64": None,
    }
    qualification["qualificationReportSha256"] = {
        "linux-x64": None,
        "windows-x64": None,
    }
    return lock


def package_members(lock: dict[str, object], package_name: str) -> set[str]:
    members: set[str] = set()
    for platform_id in ("linux-x64", "windows-x64"):
        for spec in build_runtime.artifact_plan(lock, platform_id):
            if spec["package"] == package_name:
                members.add(spec["sourcePath"])
    return members


def create_fake_packages(
    root: Path,
    lock: dict[str, object],
    *,
    omit: tuple[str, str] | None = None,
) -> dict[str, Path]:
    paths: dict[str, Path] = {}
    for package_name in ("onnxruntime", "webgpu"):
        path = root / f"{package_name}.nupkg"
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for member in sorted(package_members(lock, package_name)):
                if omit == (package_name, member):
                    continue
                archive.writestr(member, f"{package_name}:{member}\n".encode())
        paths[package_name] = path
    return paths


def package_identity(path: Path, name: str) -> dict[str, object]:
    data = path.read_bytes()
    return {
        "id": name,
        "filename": path.name,
        "source": "https://example.invalid/package.nupkg",
        "bytes": len(data),
        "sha512": hashlib.sha512(data).hexdigest(),
    }


class WebGpuRuntimeContractTest(unittest.TestCase):
    def test_committed_lock_is_valid(self) -> None:
        build_runtime.validate_lock(locked())

    def test_complete_production_qualification_state_is_valid(self) -> None:
        lock = locked()
        build_runtime.validate_lock(lock)
        self.assertEqual(lock["qualification"]["status"], "production-qualified")

    def test_complete_pending_qualification_state_is_valid(self) -> None:
        build_runtime.validate_lock(pending_locked())

    def test_production_qualification_requires_both_platform_reports(self) -> None:
        lock = locked()
        qualification = lock["qualification"]
        qualification["status"] = "production-qualified"
        qualification["providerGatePassed"] = True
        qualification["productionArtifactQualified"] = True
        qualification["qualifiedArtifactSetSha256"] = {
            "linux-x64": "1" * 64,
            "windows-x64": "2" * 64,
        }
        qualification["qualificationReportSha256"] = {
            "linux-x64": "3" * 64,
            "windows-x64": None,
        }
        with self.assertRaisesRegex(
            build_runtime.ContractError, "consistently pending or production-qualified"
        ):
            build_runtime.validate_lock(lock)

    def test_lock_rejects_frozen_contract_mutations(self) -> None:
        mutations = [
            (
                "plugin version",
                lambda lock: lock["packages"]["webgpu"].__setitem__("version", "0.2.0"),
                "packages",
            ),
            (
                "package digest",
                lambda lock: lock["packages"]["onnxruntime"].__setitem__(
                    "sha512", "0" * 128
                ),
                "packages",
            ),
            (
                "topology",
                lambda lock: lock["topology"].__setitem__("kind", "monolithic"),
                "topology",
            ),
            (
                "Linux backend",
                lambda lock: lock["platforms"]["linux-x64"].__setitem__(
                    "graphicsBackend", "D3D12"
                ),
                "linux-x64 identity",
            ),
            (
                "Windows backend",
                lambda lock: lock["platforms"]["windows-x64"].__setitem__(
                    "graphicsBackend", "Vulkan"
                ),
                "windows-x64 identity",
            ),
            (
                "device ID support",
                lambda lock: lock["sessionOptions"].__setitem__(
                    "deviceIdSupported", True
                ),
                "sessionOptions",
            ),
            (
                "provider gate claim",
                lambda lock: lock["qualification"].__setitem__(
                    "providerGatePassed", False
                ),
                "qualification",
            ),
            (
                "provider gate integer",
                lambda lock: lock["qualification"].__setitem__("providerGatePassed", 0),
                "qualification",
            ),
            (
                "production claim",
                lambda lock: lock["qualification"].__setitem__(
                    "productionArtifactQualified", False
                ),
                "qualification",
            ),
            (
                "unsafe output",
                lambda lock: lock["platforms"]["windows-x64"]["runtimeFiles"][
                    0
                ].__setitem__("outputPath", "../onnxruntime.dll"),
                "safe POSIX relative path",
            ),
            (
                "missing runtime role",
                lambda lock: lock["platforms"]["linux-x64"]["runtimeFiles"].pop(),
                "runtimeFiles",
            ),
            (
                "schema version boolean",
                lambda lock: lock.__setitem__("schemaVersion", True),
                "schemaVersion",
            ),
            (
                "unexpected top-level field",
                lambda lock: lock.__setitem__("unlocked", True),
                "runtime lock fields",
            ),
        ]
        for name, mutate, error in mutations:
            with self.subTest(name=name):
                lock = copy.deepcopy(locked())
                mutate(lock)
                with self.assertRaisesRegex(build_runtime.ContractError, error):
                    build_runtime.validate_lock(lock)

    def test_artifact_plan_is_complete_and_uses_runtime_role_for_linux_core(
        self,
    ) -> None:
        lock = locked()
        linux = build_runtime.artifact_plan(lock, "linux-x64")
        windows = build_runtime.artifact_plan(lock, "windows-x64")
        self.assertEqual(len(linux), 15)
        self.assertEqual(len(windows), 18)
        core = next(
            item for item in linux if item["outputPath"] == "lib/libonnxruntime.so.1"
        )
        self.assertEqual(core["role"], "onnxruntime-core")
        self.assertEqual(
            {item["role"] for item in windows if item["outputPath"].startswith("lib/")},
            {
                "link-library",
                "onnxruntime-core",
                "webgpu-plugin",
                "dawn-dxcompiler",
                "dawn-dxil",
            },
        )

    def test_package_validation_accepts_exact_zip_and_rejects_tampering(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = create_fake_packages(root, locked())["webgpu"]
            identity = package_identity(path, "webgpu")
            build_runtime.validate_package(path, identity)
            path.write_bytes(path.read_bytes() + b"tamper")
            with self.assertRaisesRegex(
                build_runtime.ContractError, "byte count mismatch"
            ):
                build_runtime.validate_package(path, identity)

    def test_package_validation_rejects_duplicate_members(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "duplicate.nupkg"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("LICENSE", "one")
                archive.writestr("LICENSE", "two")
            identity = package_identity(path, "duplicate")
            with self.assertRaisesRegex(
                build_runtime.ContractError, "duplicate ZIP members"
            ):
                build_runtime.validate_package(path, identity)

    def test_offline_acquisition_requires_and_validates_cache(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package_path = root / "source.nupkg"
            with zipfile.ZipFile(package_path, "w") as archive:
                archive.writestr("LICENSE", "license")
            identity = package_identity(package_path, "test")
            cache = root / "cache"
            with self.assertRaisesRegex(
                build_runtime.ContractError, "offline package cache is missing"
            ):
                build_runtime.acquire_package(identity, cache, offline=True)
            cached = cache / str(identity["filename"])
            cached.write_bytes(package_path.read_bytes())
            self.assertEqual(
                build_runtime.acquire_package(identity, cache, offline=True), cached
            )
            cached.write_bytes(b"bad")
            with self.assertRaisesRegex(
                build_runtime.ContractError, "byte count mismatch"
            ):
                build_runtime.acquire_package(identity, cache, offline=True)

    def test_realistic_fake_packages_stage_and_validate_both_platforms(self) -> None:
        lock = locked()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            packages = create_fake_packages(root, lock)
            for platform_id, expected_files in (("linux-x64", 15), ("windows-x64", 18)):
                with self.subTest(platform=platform_id):
                    output = root / platform_id
                    manifest_path = build_runtime.stage_runtime(
                        lock, platform_id, packages, output
                    )
                    manifest = build_runtime.validate_sdk(output, lock)
                    self.assertEqual(manifest_path, output / "artifact-manifest.json")
                    self.assertEqual(manifest["platform"]["id"], platform_id)
                    self.assertEqual(
                        manifest["runtime"]["kind"], "onnxruntime-plugin-webgpu"
                    )
                    self.assertEqual(
                        manifest["runtime"]["providerName"], "WebGpuExecutionProvider"
                    )
                    self.assertEqual(
                        len(manifest["artifacts"]["files"]), expected_files
                    )
                    self.assertTrue(manifest["qualification"]["providerGatePassed"])

    def test_sdk_validation_rejects_file_and_manifest_tampering(self) -> None:
        lock = locked()
        cases = (
            "artifact",
            "undeclared",
            "nested-manifest",
            "platform",
            "packages",
            "headers",
        )
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                output = root / "sdk"
                build_runtime.stage_runtime(
                    lock, "linux-x64", create_fake_packages(root, lock), output
                )
                manifest_path = output / "artifact-manifest.json"
                manifest = json.loads(manifest_path.read_text("utf-8"))
                if case == "artifact":
                    (output / "lib" / "libonnxruntime.so.1").write_bytes(b"tampered")
                    error = "hash or byte count mismatch"
                elif case == "undeclared":
                    (output / "undeclared.txt").write_text("bad", "utf-8")
                    error = "undeclared file"
                elif case == "nested-manifest":
                    nested = output / "undeclared"
                    nested.mkdir()
                    (nested / "artifact-manifest.json").write_text("{}", "utf-8")
                    error = "undeclared file"
                elif case == "platform":
                    manifest["platform"]["architecture"] = "arm64"
                    manifest_path.write_text(json.dumps(manifest), "utf-8")
                    error = "platform identity"
                elif case == "packages":
                    manifest["packages"][0]["sha512"] = "0" * 128
                    manifest_path.write_text(json.dumps(manifest), "utf-8")
                    error = "package provenance"
                else:
                    manifest["headers"]["directory"] = "headers"
                    manifest_path.write_text(json.dumps(manifest), "utf-8")
                    error = "header manifest"
                with self.assertRaisesRegex(build_runtime.ContractError, error):
                    build_runtime.validate_sdk(output, lock)

    @unittest.skipIf(not hasattr(os, "symlink"), "symlinks are unavailable")
    def test_sdk_validation_rejects_symlinks(self) -> None:
        lock = locked()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "sdk"
            build_runtime.stage_runtime(
                lock, "linux-x64", create_fake_packages(root, lock), output
            )
            artifact = output / "lib" / "libonnxruntime.so.1"
            replacement = output / "lib" / "replacement"
            replacement.write_bytes(artifact.read_bytes())
            artifact.unlink()
            artifact.symlink_to(replacement.name)
            with self.assertRaisesRegex(build_runtime.ContractError, "not regular"):
                build_runtime.validate_sdk(output, lock)

            manifest = output / "artifact-manifest.json"
            manifest_copy = output / "manifest-copy.json"
            manifest_copy.write_bytes(manifest.read_bytes())
            manifest.unlink()
            manifest.symlink_to(manifest_copy.name)
            with self.assertRaisesRegex(
                build_runtime.ContractError, "manifest must be a regular file"
            ):
                build_runtime.validate_sdk(output, lock)

    def test_stage_failure_is_atomic_for_missing_package_member(self) -> None:
        lock = locked()
        missing = (
            "webgpu",
            "runtimes/linux-x64/native/libonnxruntime_providers_webgpu.so",
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "sdk"
            packages = create_fake_packages(root, lock, omit=missing)
            with self.assertRaisesRegex(
                build_runtime.ContractError, "package member is missing"
            ):
                build_runtime.stage_runtime(lock, "linux-x64", packages, output)
            self.assertFalse(output.exists())
            self.assertEqual(list(root.glob(".sdk.*")), [])

    def test_archive_member_rejects_paths_outside_the_archive_contract(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "package.nupkg"
            with zipfile.ZipFile(path, "w") as writer:
                writer.writestr("safe", "value")
            with zipfile.ZipFile(path) as archive:
                with self.assertRaisesRegex(
                    build_runtime.ContractError, "safe POSIX relative path"
                ):
                    build_runtime.archive_member(archive, "../safe", "test")

    def test_cmake_freezes_plugin_runtime_release_boundary(self) -> None:
        sources = {
            name: (ROOT / "cmake" / name).read_text("utf-8")
            for name in ("Dependencies.cmake", "WebGpuRuntime.cmake")
        }
        dependencies = "\n".join(sources.values())
        required = [
            "LIGHT_OCR_ONNXRUNTIME_FLAVOR",
            "LIGHT_OCR_WEBGPU_SDK_DIR",
            "LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD",
            "native-webgpu-plugin-0.1.0-ort-1.24.4-v1",
            "onnxruntime-plugin-webgpu",
            "productionArtifactQualified",
            "providerGatePassed",
            "LIGHT_OCR_HAS_WEBGPU=1",
        ]
        for token in required:
            with self.subTest(token=token):
                self.assertIn(token, dependencies)
        webgpu_runtime = sources["WebGpuRuntime.cmake"]
        self.assertIn("_qualification_hash_length EQUAL 64", webgpu_runtime)
        self.assertNotIn("[0-9a-f]{64}", webgpu_runtime)


if __name__ == "__main__":
    unittest.main()
