#!/usr/bin/env python3
"""Fetch the immutable AMD deployment payload, without installing the compiler."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.error
import urllib.request

try:
    from .sdk import digest, relative_path, validate_sdk
except ImportError:
    from sdk import digest, relative_path, validate_sdk

LOCK = Path(__file__).with_name("amdnpu.lock.json")


def fetch(cache: Path, output: Path, offline: bool = False) -> dict:
    lock = json.loads(LOCK.read_text("utf-8"))
    package = lock["package"]
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / package["filename"]
    if not archive.is_file():
        if offline:
            raise ValueError("locked AMD deployment archive is absent from the offline cache")
        temporary = cache / (package["filename"] + ".partial")
        try:
            with urllib.request.urlopen(package["url"], timeout=60) as source, temporary.open("wb") as target:
                shutil.copyfileobj(source, target)
        except urllib.error.HTTPError as error:
            # The payload is attached to the release draft during preparation.
            # gh keeps the credential on GitHub API requests, including when
            # asset downloads redirect to a separate storage host.
            if error.code != 404 or not (os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")):
                raise
            temporary.unlink(missing_ok=True)
            subprocess.run(["gh", "release", "download", package["releaseTag"],
                            "--repo", package["repository"], "--pattern", package["filename"],
                            "--output", str(temporary)], check=True)
        temporary.replace(archive)
    if archive.stat().st_size != package["bytes"] or digest(archive) != package["sha256"]:
        raise ValueError("AMD deployment archive identity mismatch")
    if output.exists():
        raise ValueError("output directory already exists; choose a fresh SDK directory")
    output.mkdir(parents=True)
    with tarfile.open(archive, "r:gz") as bundle:
        seen = set()
        for member in bundle:
            path = relative_path(member.name)
            if member.name in seen or not (member.isfile() or member.isdir()):
                raise ValueError(f"invalid AMD deployment archive entry: {member.name}")
            seen.add(member.name)
            destination = output.joinpath(*path.parts)
            if member.isdir():
                destination.mkdir(parents=True, exist_ok=True)
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            with bundle.extractfile(member) as source, destination.open("wb") as target:
                shutil.copyfileobj(source, target)
    value = validate_sdk(output, "amdnpu")
    if value["artifactSetSha256"] != lock["artifactSetSha256"]:
        raise ValueError("AMD deployment artifact set does not match the lock")
    return value


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-cache", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    value = fetch(args.package_cache, args.output_dir, args.offline)
    print(f"AMD deployment SDK: {value['artifactSetSha256']} (hardware inference not validated)")


if __name__ == "__main__":
    main()
