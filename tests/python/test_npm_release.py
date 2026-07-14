from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform as host_platform
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from tools import npm_release


class NpmReleaseTests(unittest.TestCase):
    def test_rejects_a_pre_tiled_package_version(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "0.2.0 or newer"):
            npm_release.assemble(
                argparse.Namespace(
                    version="0.1.1",
                    bundle=Path("unused"),
                    native_root=Path("unused"),
                    output_dir=Path("unused"),
                )
            )

    def test_rejects_a_version_that_does_not_match_the_source(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "does not match source version"):
            npm_release.assemble(
                argparse.Namespace(
                    version="0.2.1",
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

    @mock.patch("tools.npm_release.time.sleep")
    @mock.patch("tools.npm_release.npm_dist_tag")
    def test_dist_tag_verification_waits_for_registry_convergence(
        self, dist_tag: mock.Mock, sleep: mock.Mock
    ) -> None:
        dist_tag.side_effect = ["0.1.0", "0.2.0"]

        npm_release.wait_for_dist_tag(
            "npm", "@arcships/light-ocr", "latest", "0.2.0"
        )

        self.assertEqual(dist_tag.call_count, 2)
        sleep.assert_called_once_with(3)

    def test_stages_and_deterministically_packs_six_packages(self) -> None:
        npm = shutil.which("npm")
        if npm is None:
            self.skipTest("npm is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build" / "bin"
            build.mkdir(parents=True)
            (build / "light_ocr_node.node").write_bytes(b"native-addon")
            for platform in npm_release.PLATFORMS.values():
                (build / platform["runtime"]).write_bytes(platform["runtime"].encode())

            metadata = root / "metadata"
            (metadata / "licenses").mkdir(parents=True)
            (metadata / "licenses" / "dependency.txt").write_text("license\n", "utf-8")
            (metadata / "license-inventory.json").write_text(
                '{"schemaVersion":"1.0","files":[]}\n', "utf-8"
            )
            (metadata / "sbom.spdx.json").write_text(
                '{"spdxVersion":"SPDX-2.3"}\n', "utf-8"
            )

            native_root = root / "native"
            for platform_id in npm_release.PLATFORMS:
                npm_release.stage_native(
                    argparse.Namespace(
                        platform_id=platform_id,
                        build_dir=build.parent,
                        metadata_dir=metadata,
                        output_dir=native_root / platform_id,
                    )
                )

            bundle = root / "bundle"
            bundle.mkdir()
            (bundle / "manifest.json").write_text(
                json.dumps({
                    "schemaVersion": "1.0",
                    "bundleId": npm_release.BUNDLE_ID,
                    "normalizedConfigPath": "normalized-config.json",
                }) + "\n", "utf-8"
            )
            (bundle / "normalized-config.json").write_text(
                json.dumps({
                    "schemaVersion": "1.2",
                    "runtimeProfiles": {
                        "tiled": {"contractVersion": "tiled-v1"}
                    },
                }) + "\n", "utf-8"
            )

            staging = root / "staging"
            npm_release.assemble(
                argparse.Namespace(
                    version="0.2.0",
                    bundle=bundle,
                    native_root=native_root,
                    output_dir=staging,
                )
            )
            facade = json.loads((staging / "facade" / "package.json").read_text("utf-8"))
            self.assertEqual(facade["dependencies"][npm_release.MODEL_PACKAGE], "0.2.0")
            self.assertEqual(len(facade["optionalDependencies"]), 4)
            model = json.loads(
                (staging / "model-ppocrv6-small" / "package.json").read_text("utf-8")
            )
            self.assertEqual(model["lightOcr"]["manifestSchemaVersion"], "1.0")
            self.assertEqual(
                model["lightOcr"]["normalizedConfigSchemaVersion"], "1.2"
            )
            self.assertEqual(model["lightOcr"]["tiledContractVersion"], "tiled-v1")

            tarballs = root / "tarballs"
            npm_release.pack(
                argparse.Namespace(staging_dir=staging, output_dir=tarballs, npm=npm)
            )
            release = json.loads((tarballs / "release-manifest.json").read_text("utf-8"))
            self.assertEqual(release["version"], "0.2.0")
            self.assertEqual(len(release["packages"]), 6)
            self.assertEqual(len(list(tarballs.glob("*.tgz"))), 6)

            machine = host_platform.machine().lower()
            if host_platform.system() == "Darwin" and machine in {"arm64", "aarch64"}:
                platform_id = "macos-arm64"
            elif host_platform.system() == "Darwin" and machine in {"x86_64", "amd64"}:
                platform_id = "macos-x64"
            elif host_platform.system() == "Linux" and machine in {"x86_64", "amd64"}:
                platform_id = "linux-x64"
            elif host_platform.system() == "Windows" and machine in {"x86_64", "amd64"}:
                platform_id = "windows-x64"
            else:
                return
            filenames = {record["name"]: record["filename"] for record in release["packages"]}
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
            self.assertTrue((consumer / "node_modules/@arcships/light-ocr/package.json").is_file())


if __name__ == "__main__":
    unittest.main()
