#!/usr/bin/env python3
"""Extract the locked native payload and C headers; Python is never shipped."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import urllib.request
import zipfile

try:
    from .sdk import artifact_set, digest, record, relative_path, validate_sdk
except ImportError:
    from sdk import artifact_set, digest, record, relative_path, validate_sdk

LOCK = Path(__file__).with_name("openvino.lock.json")


def build(cache: Path, output: Path, offline: bool = False) -> dict:
    lock = json.loads(LOCK.read_text("utf-8"))
    package = lock["package"]
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / package["filename"]
    if not archive.is_file():
        if offline:
            raise ValueError("locked OpenVINO wheel is absent from the offline cache")
        temporary = archive.with_suffix(".partial")
        with urllib.request.urlopen(package["url"], timeout=60) as source, temporary.open("wb") as target:
            shutil.copyfileobj(source, target)
        temporary.replace(archive)
    if archive.stat().st_size != package["bytes"] or digest(archive) != package["sha256"]:
        raise ValueError("OpenVINO wheel identity mismatch")
    if output.exists():
        raise ValueError("output directory already exists; choose a fresh SDK directory")
    output.mkdir(parents=True)
    with zipfile.ZipFile(archive) as wheel:
        names = wheel.namelist()
        if len(names) != len(set(names)):
            raise ValueError("OpenVINO archive has duplicate entries")
        for name in names:
            relative_path(name.rstrip("/"))
        selected = {f"openvino/libs/{name}": f"lib/{name}" for name in lock["libraries"]}
        selected.update({name: name.removeprefix("openvino/") for name in names
                         if name.startswith("openvino/include/openvino/c/") and not name.endswith("/")})
        selected.update({name: f"licenses/{Path(name).name}" for name in names
                         if "/licenses/" in name and not name.endswith("/")})
        for source, destination in selected.items():
            target = output / destination
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(wheel.read(source))
    files = [record(path, output) for path in sorted(output.rglob("*")) if path.is_file()]
    artifacts = [item for item in files if item["path"].startswith("lib/")]
    manifest = {
        "schemaVersion": "1.0", "provider": "openvino", "platformId": "linux-x64",
        "providerVersion": lock["version"], "qualificationId": lock["qualificationId"],
        "qualificationOnly": True, "runtimeLibrary": lock["runtimeLibrary"],
        "configuration": lock["configuration"], "artifacts": artifacts,
        "artifactSetSha256": artifact_set(artifacts), "files": files,
        "licenses": [item for item in files if item["path"].startswith("licenses/")],
        "source": package,
    }
    (output / "sdk-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", "utf-8")
    return validate_sdk(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-cache", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    value = build(args.package_cache, args.output_dir, args.offline)
    print(f"OpenVINO SDK: {value['artifactSetSha256']} (qualification-only)")


if __name__ == "__main__":
    main()
