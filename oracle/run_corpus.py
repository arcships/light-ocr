#!/usr/bin/env python3
"""Run all materialized raw-pixel fixtures through the parity gate."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-probe", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument(
        "--live-oracle",
        action="store_true",
        help="compare with the pinned oracle on this machine instead of locked stage goldens",
    )
    arguments = parser.parse_args()
    runner = Path(__file__).with_name("run_parity.py")
    reports = []
    for fixture in sorted(arguments.fixtures.glob("*/fixture.json")):
        report_path = arguments.report_dir / f"{fixture.parent.name}.json"
        command = [
            str(Path(__import__("sys").executable)), str(runner),
            "--native-probe", str(arguments.native_probe),
            "--bundle", str(arguments.bundle),
            "--fixture", str(fixture),
            "--report", str(report_path),
        ]
        if arguments.live_oracle:
            command.append("--live-oracle")
        process = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        if not process.stdout:
            raise RuntimeError(f"parity runner produced no report for {fixture}: {process.stderr}")
        report = json.loads(process.stdout)
        reports.append({"fixtureId": report["fixtureId"], "passed": report["passed"],
                        "report": report_path.name})
    aggregate = {
        "schemaVersion": "1.0",
        "passed": all(report["passed"] for report in reports),
        "fixtureCount": len(reports),
        "fixtures": reports,
    }
    arguments.report_dir.mkdir(parents=True, exist_ok=True)
    (arguments.report_dir / "corpus-report.json").write_text(
        json.dumps(aggregate, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(aggregate, sort_keys=True, separators=(",", ":")))
    return 0 if aggregate["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
