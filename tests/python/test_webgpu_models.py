from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools.webgpu import package_bundle


class WebGpuModelsTest(unittest.TestCase):
    def test_tracked_fp16_artifact_satisfies_the_packaging_contract(self) -> None:
        bundle, artifact = package_bundle.locked_artifact()
        package_bundle.validate_locked_artifact(bundle, artifact)

    def test_packaging_contract_rejects_semantic_provenance_drift(self) -> None:
        bundle, artifact = package_bundle.locked_artifact()
        root = package_bundle.ROOT / artifact["directory"]
        provenance = json.loads(
            (root / artifact["provenance"]["path"]).read_text("utf-8")
        )
        provenance["models"]["detection"]["output"]["sha256"] = "0" * 64
        with mock.patch.object(
            package_bundle,
            "verify_file",
            side_effect=[json.dumps(provenance).encode(), b"det", b"rec"],
        ):
            with self.assertRaisesRegex(RuntimeError, "detection provenance"):
                package_bundle.validate_locked_artifact(bundle, artifact)

    def test_locked_file_verification_rejects_tampering(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "model.onnx"
            path.write_bytes(b"tampered")
            with self.assertRaisesRegex(RuntimeError, "locked bytes and SHA-256"):
                package_bundle.verify_file(
                    path,
                    {"bytes": 8, "sha256": "0" * 64},
                    "model",
                )


if __name__ == "__main__":
    unittest.main()
