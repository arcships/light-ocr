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


if __name__ == "__main__":
    unittest.main()
