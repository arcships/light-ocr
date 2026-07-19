from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.webgpu import package_bundle


class WebGpuModelsTest(unittest.TestCase):
    def test_tracked_fp16_artifact_matches_lock_and_provenance(self) -> None:
        bundle, artifact = package_bundle.locked_artifact()
        root = package_bundle.ROOT / artifact["directory"]
        provenance_bytes = package_bundle.verify_file(
            root / artifact["provenance"]["path"],
            artifact["provenance"],
            "provenance",
        )
        provenance = json.loads(provenance_bytes)
        self.assertEqual(provenance["artifactId"], artifact["artifactId"])
        self.assertEqual(provenance["conversionId"], artifact["conversionId"])
        self.assertEqual(
            provenance["runtimeContract"],
            {
                "precision": "fp16",
                "graphOptimizationLevel": "extended",
                "cpuPartition": "allow-required",
                "requiredCpuOperators": ["Concat", "Gather", "Slice"],
            },
        )
        source_artifacts = {
            record["name"]: record for record in bundle["artifacts"]
        }
        for kind in ("detection", "recognition"):
            data = package_bundle.verify_file(
                root / artifact[kind]["path"], artifact[kind], kind
            )
            self.assertEqual(hashlib.sha256(data).hexdigest(), artifact[kind]["sha256"])
            self.assertEqual(provenance["models"][kind]["output"], artifact[kind])
            self.assertEqual(
                provenance["models"][kind]["source"],
                {
                    "path": (
                        "det/inference.onnx"
                        if kind == "detection"
                        else "rec/inference.onnx"
                    ),
                    "bytes": source_artifacts[kind]["members"]["inference.onnx"][
                        "bytes"
                    ],
                    "sha256": source_artifacts[kind]["members"]["inference.onnx"][
                        "sha256"
                    ],
                },
            )

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
