#!/usr/bin/env python3
"""Compare the public CPU and Apple OCR contracts on the locked corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "oracle"))
from compare import box_metrics  # noqa: E402


DEFAULT_ACCEPTANCE = ROOT / "tools" / "apple" / "acceptance.json"
DEFAULT_FIXTURES = ROOT / "corpus" / "fixtures"
DEFAULT_REPORT = ROOT / "reports" / "apple" / "quality.json"


def edit_distance(left: str, right: str) -> int:
    previous = list(range(len(right) + 1))
    for left_index, left_value in enumerate(left, 1):
        current = [left_index]
        for right_index, right_value in enumerate(right, 1):
            current.append(min(
                current[-1] + 1,
                previous[right_index] + 1,
                previous[right_index - 1] + (left_value != right_value),
            ))
        previous = current
    return previous[-1]


def run_native(
    executable: Path, bundle: Path, fixture: dict[str, object],
    fixture_root: Path, profile: str,
) -> dict[str, object]:
    directory = fixture_root / str(fixture["id"])
    process = subprocess.run(
        [
            str(executable),
            "--bundle", str(bundle),
            "--pixels", str(directory / "pixels.bin"),
            "--width", str(fixture["width"]),
            "--height", str(fixture["height"]),
            "--stride", str(fixture["stride"]),
            "--format", str(fixture["pixelFormat"]),
            "--profile", profile,
            "--diagnostics",
        ],
        check=False,
        capture_output=True,
        text=True,
        timeout=180,
    )
    if process.returncode != 0:
        raise RuntimeError(
            f"{profile} failed for {fixture['id']}: "
            f"{process.stdout[-4000:]}{process.stderr[-4000:]}"
        )
    return json.loads(process.stdout)


def greedy_matches(
    cpu_lines: list[dict[str, object]], apple_lines: list[dict[str, object]],
) -> list[dict[str, object]]:
    candidates: list[tuple[float, int, int]] = []
    for cpu_index, cpu in enumerate(cpu_lines):
        for apple_index, apple in enumerate(apple_lines):
            iou, _ = box_metrics(cpu["box"], apple["box"])
            candidates.append((iou, cpu_index, apple_index))
    used_cpu: set[int] = set()
    used_apple: set[int] = set()
    matches: list[dict[str, object]] = []
    for iou, cpu_index, apple_index in sorted(candidates, reverse=True):
        if iou < 0.5:
            break
        if cpu_index in used_cpu or apple_index in used_apple:
            continue
        used_cpu.add(cpu_index)
        used_apple.add(apple_index)
        matches.append({
            "cpuIndex": cpu_index,
            "appleIndex": apple_index,
            "iou": iou,
            "confidenceDifference": abs(
                float(cpu_lines[cpu_index]["confidence"])
                - float(apple_lines[apple_index]["confidence"])
            ),
        })
    return sorted(matches, key=lambda value: int(value["cpuIndex"]))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-validate", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, default=DEFAULT_FIXTURES)
    parser.add_argument("--acceptance", type=Path, default=DEFAULT_ACCEPTANCE)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    arguments = parser.parse_args()
    acceptance = json.loads(arguments.acceptance.read_text("utf-8"))
    quality = acceptance["quality"]
    manifest = json.loads(
        (arguments.bundle.resolve() / "manifest.json").read_text("utf-8")
    )
    provider = manifest.get("providers", {}).get("apple", {})
    models = {
        "detectionPackageSha256": provider.get("detection", {}).get(
            "packageSha256"
        ),
        "recognitionPackageSha256": provider.get("recognition", {}).get(
            "packageSha256"
        ),
        "qualificationId": provider.get("qualificationId"),
    }
    expected_models = acceptance["models"]
    if models != {
        "detectionPackageSha256": expected_models["detectionPackageSha256"],
        "recognitionPackageSha256": expected_models["recognitionPackageSha256"],
        "qualificationId": acceptance["qualificationId"],
    }:
        raise RuntimeError("quality bundle differs from the locked Apple models")
    fixture_root = arguments.fixtures.resolve()
    fixtures = [
        json.loads(path.read_text("utf-8"))
        for path in sorted(fixture_root.glob("*/fixture.json"))
    ]
    records: list[dict[str, object]] = []
    cpu_characters = 0
    character_errors = 0
    cpu_lines_total = 0
    matched_lines = 0
    iou_total = 0.0
    confidence_difference_total = 0.0
    critical_failures: list[str] = []
    for fixture in fixtures:
        cpu = run_native(
            arguments.native_validate.resolve(), arguments.bundle.resolve(),
            fixture, fixture_root, "bounded_default",
        )
        apple = run_native(
            arguments.native_validate.resolve(), arguments.bundle.resolve(),
            fixture, fixture_root, "apple_interactive",
        )
        cpu_text = "\n".join(line["text"] for line in cpu["lines"])
        apple_text = "\n".join(line["text"] for line in apple["lines"])
        errors = edit_distance(cpu_text, apple_text)
        matches = greedy_matches(cpu["lines"], apple["lines"])
        cpu_characters += len(cpu_text)
        character_errors += errors
        cpu_lines_total += len(cpu["lines"])
        matched_lines += len(matches)
        iou_total += sum(float(match["iou"]) for match in matches)
        confidence_difference_total += sum(
            float(match["confidenceDifference"]) for match in matches
        )
        exact = [line["text"] for line in cpu["lines"]] == [
            line["text"] for line in apple["lines"]
        ]
        if fixture["id"] in quality["criticalFixtureIds"] and not exact:
            critical_failures.append(str(fixture["id"]))
        records.append({
            "fixtureId": fixture["id"],
            "cpuText": [line["text"] for line in cpu["lines"]],
            "appleText": [line["text"] for line in apple["lines"]],
            "exactText": exact,
            "characterErrors": errors,
            "cpuCharacters": len(cpu_text),
            "cpuLineCount": len(cpu["lines"]),
            "appleLineCount": len(apple["lines"]),
            "matches": matches,
        })

    character_similarity = (
        1.0 - character_errors / cpu_characters if cpu_characters else 1.0
    )
    detection_recall = matched_lines / cpu_lines_total if cpu_lines_total else 1.0
    mean_iou = iou_total / matched_lines if matched_lines else 1.0
    mean_confidence_difference = (
        confidence_difference_total / matched_lines if matched_lines else 0.0
    )
    failures: list[str] = []
    if character_similarity < quality["minimumCharacterSimilarity"]:
        failures.append("character similarity is below the locked threshold")
    if detection_recall < quality["minimumDetectionRecallAgainstCpu"]:
        failures.append("detection recall against CPU is below the locked threshold")
    if mean_iou < quality["minimumMeanMatchedIoU"]:
        failures.append("matched box IoU is below the locked threshold")
    if mean_confidence_difference > quality["maximumMeanMatchedConfidenceDifference"]:
        failures.append("confidence drift exceeds the locked threshold")
    if critical_failures:
        failures.append("critical fixture text changed")
    report: dict[str, object] = {
        "schemaVersion": "1.0",
        "qualificationId": acceptance["qualificationId"],
        "device": platform.platform(),
        "acceptanceSha256": hashlib.sha256(arguments.acceptance.read_bytes()).hexdigest(),
        "models": models,
        "passed": not failures,
        "failures": failures,
        "metrics": {
            "fixtureCount": len(records),
            "characterSimilarity": character_similarity,
            "detectionRecallAgainstCpu": detection_recall,
            "meanMatchedIoU": mean_iou,
            "meanMatchedConfidenceDifference": mean_confidence_difference,
            "criticalFailures": critical_failures,
        },
        "fixtures": records,
    }
    encoded = json.dumps(report, ensure_ascii=False, sort_keys=True,
                         separators=(",", ":"))
    report["reportSha256"] = hashlib.sha256(encoded.encode("utf-8")).hexdigest()
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({
        "passed": report["passed"],
        "metrics": report["metrics"],
        "report": str(arguments.report),
    }, ensure_ascii=False, sort_keys=True))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
