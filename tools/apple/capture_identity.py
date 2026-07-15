#!/usr/bin/env python3
"""Capture the real Apple Silicon identity used for local qualification."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--expected-device-family",
        required=True,
        choices=("Apple M1", "Apple M2", "Apple M3", "Apple M4"),
    )
    parser.add_argument("--runner-label", default="local-apple-silicon")
    parser.add_argument("--report", type=Path, required=True)
    arguments = parser.parse_args()

    brand = subprocess.check_output(
        ["sysctl", "-n", "machdep.cpu.brand_string"], text=True
    ).strip()
    if not brand.startswith(arguments.expected_device_family):
        parser.error(
            f"device {brand!r} does not match {arguments.expected_device_family!r}"
        )
    identity = {
        "schemaVersion": "1.0",
        "expectedDeviceFamily": arguments.expected_device_family,
        "deviceBrand": brand,
        "operatingSystem": platform.platform(),
        "runnerLabel": arguments.runner_label,
    }
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(
        json.dumps(identity, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(identity, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
