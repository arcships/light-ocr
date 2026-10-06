#!/usr/bin/env python3
"""Create a release SDK from immutable candidate bytes and reviewed reports."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil

try:
    from .sdk import record, validate_acceptance, validate_sdk
except ImportError:
    from sdk import record, validate_acceptance, validate_sdk


def accept(source: Path, reports: list[Path], output: Path, qualification_id: str) -> dict:
    manifest = validate_sdk(source)
    if not manifest["qualificationOnly"]:
        raise ValueError("acceptance input must be a candidate SDK")
    values = [json.loads(path.read_text("utf-8")) for path in reports]
    validate_acceptance(manifest, values)
    if output.exists() or not qualification_id.strip():
        raise ValueError("select a fresh output directory and a reviewed qualification ID")
    shutil.copytree(source, output)
    directory = output / "qualification"
    directory.mkdir()
    records = []
    for index, path in enumerate(reports):
        target = directory / f"report-{index}.json"
        shutil.copyfile(path, target)
        records.append(record(target, output))
    manifest["files"].extend(records)
    manifest["acceptance"] = {"artifactSetSha256": manifest["artifactSetSha256"],
                              "providerGatePassed": True, "reports": records}
    manifest["qualificationOnly"] = False
    manifest["qualificationId"] = qualification_id
    (output / "sdk-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", "utf-8")
    return validate_sdk(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk-dir", type=Path, required=True)
    parser.add_argument("--report", type=Path, action="append", required=True)
    parser.add_argument("--qualification-id", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    value = accept(args.sdk_dir, args.report, args.output_dir, args.qualification_id)
    print(f"Accepted {value['provider']}: {value['artifactSetSha256']}")


if __name__ == "__main__":
    main()
