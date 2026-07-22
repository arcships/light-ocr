from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from tests.python.npm_release_fixtures import (
    current_platform_id,
    model_bundle,
    stage_cpu_native_packages,
)
from tools import npm_release


class NpmReleaseTests(unittest.TestCase):
    def test_multi_config_build_file_is_configuration_isolated(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "bin" / "Release").mkdir(parents=True)
            (build / "bin" / "light_ocr_node.node").write_bytes(b"stale")
            expected = build / "bin" / "Release" / "light_ocr_node.node"
            expected.write_bytes(b"release")
            (build / "CMakeCache.txt").write_text(
                "CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n"
                "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n",
                "utf-8",
            )

            self.assertEqual(
                npm_release.build_file(build, "light_ocr_node.node", "Release"),
                expected,
            )
            with self.assertRaisesRegex(RuntimeError, "Debug build output is missing"):
                npm_release.build_file(build, "light_ocr_node.node", "Debug")

    def test_single_config_generator_ignores_stale_configuration_types(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            binary = build / "bin" / "light_ocr_node.node"
            binary.parent.mkdir()
            binary.write_bytes(b"release")
            (build / "CMakeCache.txt").write_text(
                "CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n"
                "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n",
                "utf-8",
            )
            self.assertEqual(
                npm_release.build_file(build, "light_ocr_node.node", "Release"),
                binary,
            )

    def test_rejects_a_version_that_does_not_match_the_source(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "does not match source version"):
            npm_release.assemble(
                argparse.Namespace(
                    version="0.2.2",
                    bundle=Path("unused"),
                    native_root=Path("unused"),
                    output_dir=Path("unused"),
                )
            )

    @mock.patch("tools.npm_release.subprocess.run")
    def test_registry_lookup_bypasses_stale_npm_metadata(self, run: mock.Mock) -> None:
        run.return_value = subprocess.CompletedProcess(
            args=[], returncode=0, stdout='"sha512-release-integrity"\n', stderr=""
        )

        integrity = npm_release.npm_integrity("npm", "@arcships/light-ocr@0.1.0")

        self.assertEqual(integrity, "sha512-release-integrity")
        command = run.call_args.args[0]
        self.assertIn("--prefer-online", command)
        self.assertIn(f"--registry={npm_release.NPM_REGISTRY}", command)

    @mock.patch("tools.npm_release.npm_integrity", return_value=None)
    def test_unpublished_release_can_enter_the_expensive_pipeline(
        self, integrity: mock.Mock
    ) -> None:
        npm_release.ensure_unpublished(
            argparse.Namespace(version=npm_release.SOURCE_VERSION, npm="npm")
        )

        integrity.assert_called_once_with(
            "npm", f"{npm_release.FACADE_PACKAGE}@{npm_release.SOURCE_VERSION}"
        )

    @mock.patch(
        "tools.npm_release.npm_integrity", return_value="sha512-published-integrity"
    )
    def test_published_release_must_use_the_promotion_workflow(
        self, integrity: mock.Mock
    ) -> None:
        with self.assertRaisesRegex(
            RuntimeError, "already published.*npm promote workflow"
        ):
            npm_release.ensure_unpublished(
                argparse.Namespace(version=npm_release.SOURCE_VERSION, npm="npm")
            )

        integrity.assert_called_once()

    def test_unpublished_guard_rejects_a_version_that_does_not_match_source(
        self,
    ) -> None:
        with self.assertRaisesRegex(RuntimeError, "does not match source version"):
            npm_release.ensure_unpublished(
                argparse.Namespace(version="999.0.0", npm="npm")
            )

    @mock.patch("tools.npm_release.time.sleep")
    @mock.patch("tools.npm_release.npm_dist_tag")
    def test_dist_tag_verification_waits_for_registry_convergence(
        self, dist_tag: mock.Mock, sleep: mock.Mock
    ) -> None:
        dist_tag.side_effect = ["0.1.0", "0.2.0"]

        npm_release.wait_for_dist_tag("npm", "@arcships/light-ocr", "latest", "0.2.0")

        self.assertEqual(dist_tag.call_count, 2)
        sleep.assert_called_once_with(3)

    def test_stages_and_deterministically_packs_all_release_packages(self) -> None:
        npm = shutil.which("npm")
        if npm is None:
            self.skipTest("npm is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native_root = stage_cpu_native_packages(root)
            for platform_id in ("macos-arm64", "macos-x64"):
                descriptor = json.loads(
                    (
                        native_root / platform_id / "native" / "runtime-descriptor.json"
                    ).read_text("utf-8")
                )
                expected = (
                    ["apple", "cpu"]
                    if platform_id == "macos-arm64"
                    else ["cpu"]
                )
                self.assertEqual(descriptor["autoPolicy"]["providers"], expected)
                self.assertEqual(
                    set(descriptor["providers"]),
                    {"apple", "cpu"} if platform_id == "macos-arm64" else {"cpu"},
                )

            staging = root / "staging"
            source_version = npm_release.read_json(
                npm_release.ROOT / "bindings" / "node" / "package.json"
            )["version"]
            npm_release.assemble(
                argparse.Namespace(
                    version=source_version,
                    bundle=model_bundle(root),
                    native_root=native_root,
                    output_dir=staging,
                )
            )
            facade = json.loads(
                (staging / "facade" / "package.json").read_text("utf-8")
            )
            self.assertEqual(facade["bin"], {"light-ocr": "./bin/light-ocr.cjs"})
            self.assertIn("bin/", facade["files"])
            self.assertTrue(
                (staging / "facade" / "bin" / "light-ocr.cjs").is_file()
            )
            self.assertEqual(
                facade["dependencies"][npm_release.MODEL_PACKAGE], source_version
            )
            self.assertEqual(
                len(facade["optionalDependencies"]), len(npm_release.PLATFORMS)
            )
            model = json.loads(
                (staging / "model-ppocrv6-small" / "package.json").read_text("utf-8")
            )
            self.assertEqual(model["lightOcr"]["manifestSchemaVersion"], "1.2")
            self.assertEqual(model["lightOcr"]["normalizedConfigSchemaVersion"], "1.2")
            self.assertEqual(model["lightOcr"]["tiledContractVersion"], "tiled-v1")

            tarballs = root / "tarballs"
            npm_release.pack(
                argparse.Namespace(staging_dir=staging, output_dir=tarballs, npm=npm)
            )
            release = json.loads(
                (tarballs / "release-manifest.json").read_text("utf-8")
            )
            self.assertEqual(release["version"], source_version)
            expected_packages = len(npm_release.PLATFORMS) + 2
            self.assertEqual(len(release["packages"]), expected_packages)
            self.assertEqual(
                len(list(tarballs.glob("*.tgz"))), expected_packages
            )

            platform_id = current_platform_id()
            if platform_id is None:
                return
            filenames = {
                record["name"]: record["filename"] for record in release["packages"]
            }
            native_name = npm_release.PLATFORMS[platform_id]["package"]
            consumer = root / "consumer"
            consumer.mkdir()
            (consumer / "package.json").write_text(
                '{"name":"package-smoke","version":"1.0.0","private":true}\n', "utf-8"
            )
            subprocess.run(
                [
                    npm,
                    "install",
                    "--offline",
                    "--ignore-scripts",
                    "--no-audit",
                    "--no-fund",
                    "--package-lock=false",
                    str(tarballs / filenames[npm_release.MODEL_PACKAGE]),
                    str(tarballs / filenames[native_name]),
                    str(tarballs / filenames[npm_release.FACADE_PACKAGE]),
                ],
                cwd=consumer,
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertTrue(
                (consumer / "node_modules/@arcships/light-ocr/package.json").is_file()
            )
            cli = consumer / "node_modules" / ".bin" / (
                "light-ocr.cmd" if os.name == "nt" else "light-ocr"
            )
            completed = subprocess.run(
                [str(cli), "info", "--version"],
                cwd=consumer,
                check=True,
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            self.assertEqual(json.loads(completed.stdout)["npm"], source_version)

    def test_runtime_descriptor_rejects_mutated_payload_and_qualification_release(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = root / "native"
            native.mkdir()
            addon = native / "light_ocr_node.node"
            runtime = native / "libonnxruntime.so.1"
            addon.write_bytes(b"addon")
            runtime.write_bytes(b"runtime")
            descriptor = {
                "schemaVersion": "2.0",
                "platform": {
                    "id": "linux-x64",
                    "os": "linux",
                    "architecture": "x86_64",
                    "libc": "glibc",
                },
                "runtime": {
                    "flavor": "cpu",
                    "kind": "onnxruntime-cpu",
                    "version": "1.22.0",
                    "abi": "onnxruntime-c-api-22",
                    "artifacts": [npm_release.file_record(runtime, root)],
                },
                "qualificationOnly": False,
                "released": True,
                "autoPolicy": {
                    "id": "linux-x64-v1",
                    "version": 1,
                    "providers": ["cpu"],
                },
                "providers": {
                    "cpu": {
                        "runtimeProvider": "CPUExecutionProvider",
                        "qualificationId": "cpu-baseline-v1",
                        "artifacts": [npm_release.file_record(runtime, root)],
                    }
                },
                "addon": npm_release.file_record(addon, root),
            }
            npm_release.validate_runtime_descriptor(
                descriptor, root, platform_id="linux-x64", require_released=True
            )
            runtime.write_bytes(b"changed")
            with self.assertRaisesRegex(RuntimeError, "(?:byte count|hash) mismatch"):
                npm_release.validate_runtime_descriptor(descriptor, root)
            runtime.write_bytes(b"runtime")
            descriptor["qualificationOnly"] = True
            descriptor["released"] = False
            with self.assertRaisesRegex(RuntimeError, "cannot enter npm release"):
                npm_release.validate_runtime_descriptor(
                    descriptor, root, require_released=True
                )

if __name__ == "__main__":
    unittest.main()
