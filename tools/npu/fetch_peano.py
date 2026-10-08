#!/usr/bin/env python3
"""Extract a hash-locked build-only Peano toolchain, preserving executable modes."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import urllib.request
import zipfile

LOCK = Path(__file__).with_name("iree-source.lock.json")


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch(cache: Path, output: Path, offline=False) -> Path:
    package = json.loads(LOCK.read_text("utf-8"))["peano"]
    if output.exists():
        raise ValueError("toolchain output exists; select a fresh directory")
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / package["filename"]
    if not archive.is_file():
        if offline:
            raise ValueError("locked Peano wheel is absent from offline cache")
        temporary = archive.with_suffix(".partial")
        with urllib.request.urlopen(package["url"], timeout=60) as source, temporary.open("wb") as target:
            shutil.copyfileobj(source, target)
        if temporary.stat().st_size != package["bytes"] or digest(temporary) != package["sha256"]:
            raise ValueError("downloaded Peano wheel identity mismatch")
        temporary.replace(archive)
    if archive.stat().st_size != package["bytes"] or digest(archive) != package["sha256"]:
        raise ValueError("Peano wheel identity mismatch")
    with zipfile.ZipFile(archive) as wheel:
        entries = wheel.infolist()
        names = [item.filename for item in entries]
        if len(names) != len(set(names)):
            raise ValueError("Peano archive contains duplicate paths")
        for item in entries:
            relative = PurePosixPath(item.filename)
            if relative.is_absolute() or ".." in relative.parts or "\\" in item.filename:
                raise ValueError("unsafe Peano archive path")
            if stat.S_ISLNK(item.external_attr >> 16):
                raise ValueError("symbolic links are unsupported in the Peano archive")
        output.mkdir(parents=True)
        for item in entries:
            if not item.filename.startswith("llvm-aie/") or item.is_dir():
                continue
            target = output / item.filename
            target.parent.mkdir(parents=True, exist_ok=True)
            with wheel.open(item) as source, target.open("wb") as destination:
                shutil.copyfileobj(source, destination)
            target.chmod(0o755 if item.external_attr >> 16 & 0o111 else 0o644)
    root = output / "llvm-aie"
    if not (root / "bin/clang").is_file():
        raise ValueError("locked toolchain lacks clang")
    record = {"schemaVersion": "1.0", "stage": "build-only", "source": package,
              "binaries": [{"path": f"bin/{name}", "sha256": digest(root / "bin" / name)}
                           for name in ("clang", "ld.lld")]}
    (root / "toolchain-source.json").write_text(json.dumps(record, indent=2) + "\n", "utf-8")
    return root


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-cache", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    print(fetch(args.package_cache, args.output_dir, args.offline))


if __name__ == "__main__":
    main()
