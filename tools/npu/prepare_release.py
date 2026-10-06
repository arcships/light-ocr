#!/usr/bin/env python3
"""Validate accepted SDK inputs and expose paths to the release workflow."""
import argparse
from pathlib import Path

try:
    from .sdk import validate_sdk
except ImportError:
    from sdk import validate_sdk


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--github-env", type=Path, required=True)
    args = parser.parse_args()
    found = False
    lines = []
    for provider in ("openvino", "amdnpu"):
        directory = args.sdk_root / provider
        if not directory.exists():
            continue
        value = validate_sdk(directory, provider)
        if value["qualificationOnly"]:
            raise ValueError(f"{provider} is a candidate; release requires reviewed device reports")
        path = str(directory.resolve())
        if "\n" in path or "\r" in path:
            raise ValueError("invalid SDK directory")
        lines.append(f"{provider.upper()}_SDK_DIR={path}\n")
        found = True
    if not found:
        raise ValueError("accepted SDK artifact contains neither openvino/ nor amdnpu/")
    with args.github_env.open("a", encoding="utf-8") as stream:
        stream.writelines(lines)


if __name__ == "__main__":
    main()
