from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock
import zipfile

from tools import bootstrap_dependencies


class FakeResponse:
    def __init__(
        self,
        chunks: list[bytes | BaseException],
        status: int,
        headers: dict[str, str] | None = None,
    ) -> None:
        self.chunks = iter(chunks)
        self.status = status
        self.headers = headers or {}

    def __enter__(self) -> "FakeResponse":
        return self

    def __exit__(self, *_arguments: object) -> None:
        return None

    def getcode(self) -> int:
        return self.status

    def read(self, _size: int) -> bytes:
        try:
            value = next(self.chunks)
        except StopIteration:
            return b""
        if isinstance(value, BaseException):
            raise value
        return value


def archive_bytes() -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        member = zipfile.ZipInfo("include/value.txt")
        member.create_system = 3
        member.external_attr = (stat.S_IFREG | 0o644) << 16
        archive.writestr(member, "locked dependency")
    return output.getvalue()


def locked(data: bytes) -> dict[str, object]:
    return {
        "filename": "dependency.zip",
        "source": "https://dependencies.invalid/dependency.zip",
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


class BootstrapDependenciesTest(unittest.TestCase):
    def test_selects_common_and_one_matching_runtime(self) -> None:
        lock = {
            "dependencies": [
                {"id": "ort-cpu", "runtimeFlavor": "cpu", "platforms": ["linux-x64-gnu"]},
                {"id": "ort-webgpu", "runtimeFlavor": "webgpu", "platforms": ["linux-x64-gnu"]},
                {"id": "opencv", "runtimeFlavor": "common", "platforms": ["all"]},
            ]
        }
        selected = bootstrap_dependencies.select_dependencies(
            lock, platform_id="linux-x64-gnu", runtime_flavor="webgpu"
        )
        self.assertEqual([record["id"] for record in selected], ["ort-webgpu", "opencv"])

    def test_explains_external_webgpu_sdk_boundary(self) -> None:
        lock = {
            "dependencies": [
                {"id": "ort-cpu", "runtimeFlavor": "cpu", "platforms": ["linux-x64-gnu"]},
                {"id": "opencv", "runtimeFlavor": "common", "platforms": ["all"]},
            ]
        }
        with self.assertRaisesRegex(
            RuntimeError, "externally verified SDK.*LIGHT_OCR_WEBGPU_SDK_DIR"
        ):
            bootstrap_dependencies.select_dependencies(
                lock, platform_id="linux-x64-gnu", runtime_flavor="webgpu"
            )

    def test_rejects_missing_duplicate_or_platform_mismatched_runtime(self) -> None:
        cases = [
            (
                "missing",
                {"dependencies": [{"id": "common", "runtimeFlavor": "common", "platforms": ["all"]}]},
                "found 0",
            ),
            (
                "platform mismatch",
                {"dependencies": [{"id": "ort", "runtimeFlavor": "cpu", "platforms": ["windows-x64"]}]},
                "found 0",
            ),
            (
                "duplicate matches",
                {"dependencies": [
                    {"id": "ort-a", "runtimeFlavor": "cpu", "platforms": ["linux-x64-gnu"]},
                    {"id": "ort-b", "runtimeFlavor": "cpu", "platforms": ["linux-x64-gnu"]},
                ]},
                "found 2",
            ),
        ]
        for name, lock, error in cases:
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, error):
                bootstrap_dependencies.select_dependencies(
                    lock, platform_id="linux-x64-gnu", runtime_flavor="cpu"
                )

    def test_writes_selected_dependency_identity(self) -> None:
        records = [
            {"id": "ort", "filename": "ort.zip", "sha256": "a" * 64},
            {"id": "opencv", "filename": "opencv.tgz", "sha256": "b" * 64},
        ]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "selected.json"
            bootstrap_dependencies.write_selection(
                output,
                records,
                platform_id="linux-x64-gnu",
                runtime_flavor="cpu",
            )
            selection = json.loads(output.read_text("utf-8"))
            self.assertEqual(selection["runtimeFlavor"], "cpu")
            self.assertEqual([item["id"] for item in selection["dependencies"]], ["ort", "opencv"])

    def test_download_resumes_after_a_read_timeout(self) -> None:
        data = archive_bytes()
        split = len(data) // 2
        responses = [
            FakeResponse([data[:split], TimeoutError("stalled")], 200),
            FakeResponse(
                [data[split:]],
                206,
                {"Content-Range": f"bytes {split}-{len(data) - 1}/{len(data)}"},
            ),
        ]
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(
            bootstrap_dependencies.urllib.request,
            "urlopen",
            side_effect=responses,
        ) as urlopen, mock.patch.object(bootstrap_dependencies.time, "sleep"):
            destination = Path(directory) / "dependency.zip"
            bootstrap_dependencies.download(locked(data), destination)
            self.assertEqual(destination.read_bytes(), data)
            resumed_request = urlopen.call_args_list[1].args[0]
            self.assertEqual(resumed_request.get_header("Range"), f"bytes={split}-")

    def test_download_restarts_when_server_ignores_range(self) -> None:
        data = archive_bytes()
        split = len(data) // 2
        responses = [
            FakeResponse([data[:split], TimeoutError("stalled")], 200),
            FakeResponse([data], 200),
        ]
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(
            bootstrap_dependencies.urllib.request,
            "urlopen",
            side_effect=responses,
        ), mock.patch.object(bootstrap_dependencies.time, "sleep"):
            destination = Path(directory) / "dependency.zip"
            bootstrap_dependencies.download(locked(data), destination)
            self.assertEqual(destination.read_bytes(), data)


if __name__ == "__main__":
    unittest.main()
