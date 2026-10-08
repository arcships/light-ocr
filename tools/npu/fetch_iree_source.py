#!/usr/bin/env python3
"""Populate the fixed AMD AIE source tree and only required submodules."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

LOCK = Path(__file__).with_name("iree-source.lock.json")


def head(path: Path) -> str:
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def fetch(output: Path, offline=False, jobs=3) -> Path:
    if jobs < 1:
        raise ValueError("jobs must be positive")
    lock = json.loads(LOCK.read_text("utf-8"))
    output = output.resolve()
    if not output.exists():
        if offline:
            raise ValueError("source checkout is absent from offline cache")
        output.mkdir(parents=True)
        subprocess.run(["git", "init", str(output)], check=True)
        subprocess.run(["git", "-C", str(output), "remote", "add", "origin",
                        lock["amdAie"]["repository"]], check=True)
        subprocess.run(["git", "-C", str(output), "fetch", "--depth", "1", "origin",
                        lock["amdAie"]["commit"]], check=True)
        subprocess.run(["git", "-C", str(output), "checkout", "--detach", "FETCH_HEAD"], check=True)
    if not (output / ".git").is_dir() or head(output) != lock["amdAie"]["commit"]:
        raise ValueError("source output must be a standalone checkout of the locked commit")
    roots = [(output, lock["amdAie"]["submodules"]),
             (output / "third_party/iree", lock["ireeSubmodules"])]
    for root, revisions in roots:
        if not offline:
            # Parent checkout pins these gitlinks. No recursive unused SDKs,
            # driver installers, test suites, or release writes are invoked.
            subprocess.run(["git", "-C", str(root), "submodule", "update", "--init",
                            "--depth", "1", "--jobs", str(jobs), *revisions], check=True)
        for relative, expected in revisions.items():
            path = root / relative
            if not (path / ".git").exists() or head(path) != expected:
                raise ValueError(f"source submodule identity mismatch: {relative}")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--jobs", type=int, default=3)
    args = parser.parse_args()
    print(fetch(args.output_dir, args.offline, args.jobs))


if __name__ == "__main__":
    main()
