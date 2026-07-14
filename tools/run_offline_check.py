#!/usr/bin/env python3
"""Run the native validator from a sterile directory and optional network namespace."""

from __future__ import annotations

import argparse
import errno
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile


def assert_network_disabled() -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as connection:
        connection.settimeout(0.2)
        code = connection.connect_ex(("1.1.1.1", 53))
    if code not in {errno.ENETDOWN, errno.ENETUNREACH, errno.EHOSTUNREACH}:
        raise RuntimeError(f"network namespace is not disabled (connect_ex={code})")


def run(arguments: list[str], cwd: Path, locale: str) -> dict[str, object]:
    environment = {"PATH": os.defpath, "LC_ALL": locale, "LANG": locale, "TZ": "UTC"}
    completed = subprocess.run(
        arguments, cwd=cwd, env=environment, check=False, capture_output=True, text=True,
        encoding="utf-8", timeout=120,
    )
    if completed.returncode != 0 or completed.stderr or len(completed.stdout.splitlines()) != 1:
        raise RuntimeError(
            f"validator failed in sterile environment: exit={completed.returncode}, "
            f"stdout={completed.stdout!r}, stderr={completed.stderr!r}"
        )
    result = json.loads(completed.stdout)
    if result.get("ok") is not True:
        raise RuntimeError(f"validator returned an error: {result}")
    result.pop("timingUs", None)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--validate", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--require-network-disabled", action="store_true")
    arguments = parser.parse_args()
    validate = arguments.validate.resolve()
    if not validate.is_file() and validate.with_suffix(".exe").is_file():
        validate = validate.with_suffix(".exe")
    bundle = arguments.bundle.resolve()
    fixture_path = arguments.fixture.resolve()
    fixture = json.loads(fixture_path.read_text("utf-8"))
    pixels = fixture_path.parent / "pixels.bin"
    command = [
        str(validate), "--bundle", str(bundle), "--pixels", str(pixels),
        "--width", str(fixture["width"]), "--height", str(fixture["height"]),
        "--stride", str(fixture["stride"]), "--format", fixture["pixelFormat"],
    ]
    if arguments.require_network_disabled:
        assert_network_disabled()
    with (
        tempfile.TemporaryDirectory(prefix="light-ocr-offline-a-") as first,
        tempfile.TemporaryDirectory(prefix="light-ocr-offline-b-") as second,
    ):
        first_result = run(command, Path(first), "C")
        second_result = run(command, Path(second), "C")
    if first_result != second_result:
        raise RuntimeError("validator result depends on current directory or sterile process state")
    print(json.dumps({"schemaVersion": "1.0", "passed": True,
                      "networkDisabled": arguments.require_network_disabled},
                     sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
