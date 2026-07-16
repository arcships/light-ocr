#!/usr/bin/env python3
"""Promote a reviewed Apple provider qualification candidate to a source contract."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

try:
    from .collect_qualification import read_json, report_hash
except ImportError:  # Direct script execution.
    from collect_qualification import read_json, report_hash


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--approved-by-commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    candidate = read_json(arguments.candidate.resolve())
    if candidate.get("schema") != "light-ocr-apple-provider-baselines/1.0":
        parser.error("candidate has an unsupported schema")
    if candidate.get("status") != "candidate":
        parser.error("input must be a qualification candidate")
    candidate_hash = str(candidate.get("reportSha256", ""))
    if candidate_hash != report_hash(candidate):
        parser.error("candidate report hash does not match its contents")
    approval = arguments.approved_by_commit.lower()
    if len(approval) != 40 or any(value not in "0123456789abcdef" for value in approval):
        parser.error("--approved-by-commit must be a full Git SHA-1")

    accepted = dict(candidate)
    accepted["status"] = "accepted"
    accepted["approvedByCommit"] = approval
    accepted["candidateReportSha256"] = candidate_hash
    accepted["reportSha256"] = report_hash(accepted)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(accepted, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({
        "accepted": True,
        "families": accepted.get("qualifiedDeviceFamilies", []),
        "output": str(arguments.output),
        "reportSha256": accepted["reportSha256"],
    }, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
