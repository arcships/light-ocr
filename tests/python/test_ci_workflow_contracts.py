from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


def workflow_step(source: str, name: str) -> str:
    marker = f"      - name: {name}\n"
    start = source.index(marker)
    end = source.find("\n      - ", start + len(marker))
    return source[start:] if end == -1 else source[start:end]


class CiWorkflowContractTests(unittest.TestCase):
    def test_core_packages_the_locked_model_before_model_bound_metadata(self) -> None:
        source = (ROOT / ".github/workflows/core.yml").read_text("utf-8")
        bootstrap = workflow_step(source, "Bootstrap locked dependencies and models")
        metadata = workflow_step(
            source, "Generate build manifest, licenses, and SPDX SBOM"
        )

        self.assertIn("python tools/package_model_bundle.py", bootstrap)
        self.assertNotIn("--model-free", metadata)

    def test_webgpu_native_metadata_is_model_free(self) -> None:
        source = (ROOT / ".github/workflows/webgpu-native.yml").read_text("utf-8")
        metadata = workflow_step(
            source, "Generate license, SBOM, and native package input"
        )

        self.assertIn("python tools/generate_release_metadata.py", metadata)
        self.assertIn("--model-free", metadata)

    def test_musl_release_jobs_pin_the_target_and_dependency_selection(self) -> None:
        source = (ROOT / ".github/workflows/npm-release.yml").read_text("utf-8")
        musl_job = source[source.index("build-native-musl"):]

        self.assertIn("smoke-musl", musl_job)
        self.assertIn("container: alpine:3.22", musl_job)
        bootstrap = musl_job[musl_job.index("Bootstrap pinned native dependencies"):]
        self.assertIn("--platform-id", bootstrap[:1200])
        configure = musl_job[musl_job.index("Configure native package"):]
        self.assertIn("-DLIGHT_OCR_TARGET_LIBC=musl", configure[:2000])

    def test_musl_runtime_rebuild_workflow_pins_the_expected_artifacts(self) -> None:
        source = (ROOT / ".github/workflows/onnxruntime-musl.yml").read_text("utf-8")

        self.assertIn("musl-runtime-", source)
        self.assertIn("onnxruntime_BUILD_UNIT_TESTS=OFF", source)
        self.assertIn("FETCHCONTENT_SOURCE_DIR_EIGEN3", source)
        self.assertIn(
            "runtimes/linux-${{ matrix.arch }}-musl/native/libonnxruntime.so", source
        )


if __name__ == "__main__":
    unittest.main()
