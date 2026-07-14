#!/usr/bin/env python3
"""Create and verify a deterministic USTAR archive for a generated model bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "models" / "bundles.lock.json"
DEFAULT_BUNDLE = ROOT / "models" / "generated" / "ppocrv6-small-onnx-20260713.1"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def add_entry(archive: tarfile.TarFile, source: Path, archive_name: str) -> None:
    stat = source.lstat()
    if source.is_symlink() or not (source.is_dir() or source.is_file()):
        raise RuntimeError(f"unsupported bundle entry: {source}")
    info = tarfile.TarInfo(archive_name + ("/" if source.is_dir() else ""))
    info.uid = 0
    info.gid = 0
    info.uname = "root"
    info.gname = "root"
    info.mtime = 0
    if source.is_dir():
        info.type = tarfile.DIRTYPE
        info.mode = 0o755
        archive.addfile(info)
        return
    info.type = tarfile.REGTYPE
    info.mode = 0o644
    info.size = stat.st_size
    with source.open("rb") as payload:
        archive.addfile(info, payload)


def expected_archive(bundle_id: str) -> dict[str, object] | None:
    lock = json.loads(LOCK_PATH.read_text("utf-8"))
    bundle = next(
        (record for record in lock["bundles"] if record["bundleId"] == bundle_id), None
    )
    if bundle is None:
        raise RuntimeError(f"bundle is not present in {LOCK_PATH}: {bundle_id}")
    return bundle.get("bundleArchive")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, default=DEFAULT_BUNDLE)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--allow-unlocked",
        action="store_true",
        help="create an archive before its final hash has been recorded in bundles.lock.json",
    )
    arguments = parser.parse_args()
    bundle = arguments.bundle.resolve()
    if not bundle.is_dir() or not (bundle / "manifest.json").is_file():
        raise RuntimeError(f"generated bundle is missing or invalid: {bundle}")
    bundle_id = bundle.name
    output = (arguments.output or (bundle.parent / f"{bundle_id}.tar")).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    file_paths: list[Path] = []
    directory_paths: set[Path] = {bundle}
    for path in bundle.rglob("*"):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise RuntimeError(f"unsupported bundle entry: {path}")
        if path.is_dir():
            directory_paths.add(path)
        else:
            file_paths.append(path)
            directory_paths.update(path.parents)
    directory_paths = {path for path in directory_paths if path == bundle or bundle in path.parents}

    with tempfile.NamedTemporaryFile(
        prefix=output.name + ".", suffix=".tmp", dir=output.parent, delete=False
    ) as temporary_file:
        temporary = Path(temporary_file.name)
    try:
        with tarfile.open(temporary, mode="w", format=tarfile.USTAR_FORMAT) as archive:
            for path in sorted(directory_paths, key=lambda item: item.relative_to(bundle).as_posix()):
                relative = path.relative_to(bundle).as_posix()
                name = bundle_id if relative == "." else f"{bundle_id}/{relative}"
                add_entry(archive, path, name)
            for path in sorted(file_paths, key=lambda item: item.relative_to(bundle).as_posix()):
                add_entry(archive, path, f"{bundle_id}/{path.relative_to(bundle).as_posix()}")

        record = {"filename": output.name, "format": "ustar", "bytes": temporary.stat().st_size,
                  "sha256": sha256(temporary)}
        expected = expected_archive(bundle_id)
        if expected is None and not arguments.allow_unlocked:
            raise RuntimeError("bundleArchive is not locked; use --allow-unlocked only to obtain the initial hash")
        if expected is not None:
            for field in ("filename", "format", "bytes", "sha256"):
                if record[field] != expected[field]:
                    raise RuntimeError(
                        f"bundle archive {field} mismatch: expected {expected[field]!r}, got {record[field]!r}"
                    )
        os.replace(temporary, output)
        checksum = output.with_name(output.name + ".sha256")
        checksum.write_text(f"{record['sha256']}  {output.name}\n", encoding="ascii")
        print(json.dumps(record, sort_keys=True, separators=(",", ":")))
        return 0
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
