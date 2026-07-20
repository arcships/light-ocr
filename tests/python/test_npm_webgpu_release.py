from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tests.python.npm_release_fixtures import webgpu_stage_inputs
from tests.python.webgpu_runtime_fixtures import locked
from tools import npm_release
from tools.webgpu import build_runtime


class NpmWebGpuReleaseTests(unittest.TestCase):
    def test_rejects_pending_release_but_stages_qualification_packages(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock = copy.deepcopy(locked())
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
            packages, build_dir, metadata = webgpu_stage_inputs(root, lock)

            for platform_id, expected_names in (
                (
                    "linux-x64",
                    {"libonnxruntime.so.1", "libonnxruntime_providers_webgpu.so"},
                ),
                (
                    "windows-x64",
                    {
                        "onnxruntime.dll",
                        "onnxruntime_providers_webgpu.dll",
                        "dxcompiler.dll",
                        "dxil.dll",
                    },
                ),
            ):
                with self.subTest(platform=platform_id):
                    sdk = root / f"sdk-{platform_id}"
                    manifest_path = build_runtime.stage_runtime(
                        lock, platform_id, packages, sdk
                    )
                    build_runtime.validate_sdk(sdk, lock)
                    output = root / f"output-{platform_id}"
                    arguments = argparse.Namespace(
                        platform_id=platform_id,
                        build_dir=build_dir,
                        metadata_dir=metadata,
                        output_dir=output,
                        runtime_flavor="webgpu",
                        webgpu_artifact_manifest=manifest_path,
                        qualification_build=False,
                    )
                    with mock.patch(
                        "tools.npm_release.webgpu_runtime.load_lock",
                        return_value=lock,
                    ):
                        with self.assertRaisesRegex(
                            RuntimeError, "accepted Linux and Windows Provider Gates"
                        ):
                            npm_release.stage_native(arguments)
                    self.assertFalse(output.exists())

                    arguments.qualification_build = True
                    with mock.patch(
                        "tools.npm_release.webgpu_runtime.load_lock",
                        return_value=lock,
                    ):
                        npm_release.stage_native(arguments)
                    descriptor = json.loads(
                        (output / "native" / "runtime-descriptor.json").read_text(
                            "utf-8"
                        )
                    )
                    self.assertEqual(descriptor["schemaVersion"], "2.0")
                    self.assertTrue(descriptor["qualificationOnly"])
                    self.assertFalse(descriptor["released"])
                    self.assertEqual(
                        descriptor["autoPolicy"]["providers"], ["webgpu", "cpu"]
                    )
                    self.assertEqual(
                        {
                            Path(record["path"]).name
                            for record in descriptor["runtime"]["artifacts"]
                        },
                        expected_names,
                    )
                    provider = descriptor["providers"]["webgpu"]
                    self.assertEqual(provider["providerVersion"], "0.1.0")
                    self.assertIn(provider["providerLibrary"], provider["artifacts"])
                    expected_library = (
                        "native/onnxruntime_providers_webgpu.dll"
                        if platform_id == "windows-x64"
                        else "native/libonnxruntime_providers_webgpu.so"
                    )
                    self.assertEqual(
                        provider["providerLibrary"]["path"], expected_library
                    )
                    with self.assertRaisesRegex(
                        RuntimeError, "cannot enter npm release"
                    ):
                        npm_release.validate_runtime_descriptor(
                            descriptor, output, require_released=True
                        )

                    provider_path = output / provider["providerLibrary"]["path"]
                    provider_path.write_bytes(b"tampered")
                    with self.assertRaisesRegex(
                        RuntimeError, "(?:byte count|hash) mismatch"
                    ):
                        npm_release.validate_runtime_descriptor(descriptor, output)

    def test_stages_production_only_after_both_platforms_are_bound(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock = locked()
            packages, build_dir, metadata = webgpu_stage_inputs(root, lock)
            artifact_hashes: dict[str, str] = {}
            for platform_id in ("linux-x64", "windows-x64"):
                sdk = root / f"pending-{platform_id}"
                build_runtime.stage_runtime(lock, platform_id, packages, sdk)
                manifest = build_runtime.validate_sdk(sdk, lock)
                artifact_hashes[platform_id] = manifest["artifacts"][
                    "artifactSetSha256"
                ]

            qualification = lock["qualification"]
            qualification["status"] = "production-qualified"
            qualification["providerGatePassed"] = True
            qualification["productionArtifactQualified"] = True
            qualification["qualifiedArtifactSetSha256"] = artifact_hashes
            qualification["qualificationReportSha256"] = {
                "linux-x64": "3" * 64,
                "windows-x64": "4" * 64,
            }
            build_runtime.validate_lock(lock)

            for platform_id in ("linux-x64", "windows-x64"):
                with self.subTest(platform=platform_id):
                    sdk = root / f"qualified-{platform_id}"
                    manifest_path = build_runtime.stage_runtime(
                        lock, platform_id, packages, sdk
                    )
                    output = root / f"release-{platform_id}"
                    arguments = argparse.Namespace(
                        platform_id=platform_id,
                        build_dir=build_dir,
                        metadata_dir=metadata,
                        output_dir=output,
                        runtime_flavor="webgpu",
                        webgpu_artifact_manifest=manifest_path,
                        qualification_build=False,
                    )
                    with mock.patch(
                        "tools.npm_release.webgpu_runtime.load_lock",
                        return_value=lock,
                    ):
                        npm_release.stage_native(arguments)
                    descriptor = json.loads(
                        (output / "native" / "runtime-descriptor.json").read_text(
                            "utf-8"
                        )
                    )
                    self.assertFalse(descriptor["qualificationOnly"])
                    self.assertTrue(descriptor["released"])
                    npm_release.validate_runtime_descriptor(
                        descriptor,
                        output,
                        platform_id=platform_id,
                        require_released=True,
                    )


if __name__ == "__main__":
    unittest.main()
