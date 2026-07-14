from __future__ import annotations

import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest import mock
import urllib.error

from tools import bootstrap_models


def locked(data: bytes, url: str | None = None) -> dict[str, object]:
    record: dict[str, object] = {
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    if url is not None:
        record["url"] = url
    return record


class BootstrapModelsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.payloads = {
            "inference.onnx": b"locked onnx bytes",
            "inference.yml": b"locked yaml bytes",
        }
        self.artifact: dict[str, object] = {
            "name": "detection",
            "filename": "det.tar",
            "url": "https://primary.invalid/det.tar",
            "bytes": 0,
            "sha256": "unused",
            "members": {
                name: locked(data, f"https://fallback.invalid/{name}")
                for name, data in self.payloads.items()
            },
        }

    def test_archive_is_read_from_memory(self) -> None:
        archive_bytes = io.BytesIO()
        with tarfile.open(fileobj=archive_bytes, mode="w") as archive:
            directory = tarfile.TarInfo("PP-OCRv6_small_det_onnx_infer/")
            directory.type = tarfile.DIRTYPE
            archive.addfile(directory)
            for name, data in self.payloads.items():
                info = tarfile.TarInfo(f"PP-OCRv6_small_det_onnx_infer/{name}")
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))

        self.assertEqual(
            bootstrap_models.read_archive_members(archive_bytes.getvalue(), self.artifact),
            self.payloads,
        )

    def test_network_failure_uses_locked_member_sources(self) -> None:
        def fake_obtain(record: dict[str, object], _cache_dir: Path) -> bytes:
            if record["filename"] == "det.tar":
                raise urllib.error.HTTPError(str(record["url"]), 403, "Forbidden", None, None)
            return self.payloads[str(record["filename"]).removeprefix("detection-")]

        with tempfile.TemporaryDirectory() as cache, mock.patch.object(
            bootstrap_models, "obtain", side_effect=fake_obtain
        ):
            self.assertEqual(
                bootstrap_models.obtain_artifact_members(self.artifact, Path(cache)),
                self.payloads,
            )

    def test_integrity_failure_does_not_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as cache, mock.patch.object(
            bootstrap_models, "obtain", side_effect=RuntimeError("SHA-256 mismatch")
        ):
            with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                bootstrap_models.obtain_artifact_members(self.artifact, Path(cache))


if __name__ == "__main__":
    unittest.main()
