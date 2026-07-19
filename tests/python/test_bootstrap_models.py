from __future__ import annotations

import hashlib
import io
import json
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
            bootstrap_models.read_archive_members(
                archive_bytes.getvalue(), self.artifact
            ),
            self.payloads,
        )

    def test_network_failure_uses_locked_member_sources(self) -> None:
        def fake_obtain(
            record: dict[str, object], _cache_dir: Path, *, offline: bool = False
        ) -> bytes:
            self.assertFalse(offline)
            if record["filename"] == "det.tar":
                raise urllib.error.HTTPError(
                    str(record["url"]), 403, "Forbidden", None, None
                )
            return self.payloads[str(record["filename"]).removeprefix("detection-")]

        with (
            tempfile.TemporaryDirectory() as cache,
            mock.patch.object(bootstrap_models, "obtain", side_effect=fake_obtain),
        ):
            self.assertEqual(
                bootstrap_models.obtain_artifact_members(self.artifact, Path(cache)),
                self.payloads,
            )

    def test_integrity_failure_does_not_fallback(self) -> None:
        with (
            tempfile.TemporaryDirectory() as cache,
            mock.patch.object(
                bootstrap_models, "obtain", side_effect=RuntimeError("SHA-256 mismatch")
            ),
        ):
            with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                bootstrap_models.obtain_artifact_members(self.artifact, Path(cache))

    def test_offline_archive_miss_uses_cached_locked_members(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            for name, data in self.payloads.items():
                (cache / f"detection-{name}").write_bytes(data)

            self.assertEqual(
                bootstrap_models.obtain_artifact_members(
                    self.artifact, cache, offline=True
                ),
                self.payloads,
            )

    def test_offline_cache_miss_fails_without_network(self) -> None:
        with (
            tempfile.TemporaryDirectory() as temporary,
            mock.patch("urllib.request.urlopen") as urlopen,
        ):
            with self.assertRaisesRegex(
                bootstrap_models.OfflineCacheMiss,
                "offline model cache is missing detection-inference.onnx",
            ):
                bootstrap_models.obtain_artifact_members(
                    self.artifact, Path(temporary), offline=True
                )
            urlopen.assert_not_called()

    def test_failed_forced_rebuild_preserves_existing_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "bundle"
            output.mkdir()
            sentinel = output / "sentinel"
            sentinel.write_text("existing", "utf-8")
            lock = root / "bundles.lock.json"
            lock.write_text(
                json.dumps({"bundles": [{"artifacts": [self.artifact]}]}),
                "utf-8",
            )
            with mock.patch.object(bootstrap_models, "LOCK_PATH", lock):
                with self.assertRaises(bootstrap_models.OfflineCacheMiss):
                    bootstrap_models.write_bundle(
                        output,
                        root / "cache",
                        force=True,
                        offline=True,
                    )
            self.assertEqual(sentinel.read_text("utf-8"), "existing")

    def test_normalized_config_separates_source_defaults_and_ceilings(self) -> None:
        config = bootstrap_models.normalized_config("test-bundle", 18_709)
        self.assertEqual(config["schemaVersion"], "1.2")
        self.assertEqual(config["sourceDetectionResize"]["limitType"], "min")
        self.assertEqual(config["sourceDetectionResize"]["maxSideLimit"], 4_000)
        self.assertEqual(
            config["runtimeDefaults"],
            {
                "detection": {
                    "strategy": "bounded",
                    "maxSide": 960,
                    "minimumShortSide": 64,
                    "dimensionMultipleRounding": "ceil",
                },
                "recognitionBatchSize": 1,
            },
        )
        self.assertEqual(config["resourceLimits"]["maxRecognitionBatchSize"], 8)
        self.assertEqual(config["resourceLimits"]["maxDetectionTiles"], 100)
        self.assertEqual(
            config["runtimeProfiles"]["tiled"]["contractVersion"], "tiled-v1"
        )
        self.assertNotIn("resize", config["detection"])
        self.assertNotIn("defaultSize", config["recognition"]["batch"])


if __name__ == "__main__":
    unittest.main()
