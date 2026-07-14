#!/usr/bin/env python3
"""Establish the first model bundle's ground-truth quality baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from compare import box_metrics
from ground_truth import verify_ground_truth


def edit_distance(left: str, right: str) -> int:
    previous = list(range(len(right) + 1))
    for left_index, left_value in enumerate(left, 1):
        current = [left_index]
        for right_index, right_value in enumerate(right, 1):
            current.append(
                min(
                    current[-1] + 1,
                    previous[right_index] + 1,
                    previous[right_index - 1] + (left_value != right_value),
                )
            )
        previous = current
    return previous[-1]


def detection_match(predicted: list[list[list[float]]],
                    expected: list[list[list[float]]], threshold: float = 0.5) -> dict[str, object]:
    candidates = []
    for predicted_index, predicted_box in enumerate(predicted):
        for expected_index, expected_box in enumerate(expected):
            iou, _ = box_metrics(predicted_box, expected_box)
            candidates.append((iou, predicted_index, expected_index))
    matched_predicted: set[int] = set()
    matched_expected: set[int] = set()
    matches = []
    for iou, predicted_index, expected_index in sorted(candidates, reverse=True):
        if iou < threshold:
            break
        if predicted_index in matched_predicted or expected_index in matched_expected:
            continue
        matched_predicted.add(predicted_index)
        matched_expected.add(expected_index)
        matches.append({"predictedIndex": predicted_index, "expectedIndex": expected_index,
                        "iou": iou})
    true_positives = len(matches)
    false_positives = len(predicted) - true_positives
    false_negatives = len(expected) - true_positives
    precision = true_positives / (true_positives + false_positives) if predicted else 1.0
    recall = true_positives / (true_positives + false_negatives) if expected else 1.0
    hmean = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return {
        "iouThreshold": threshold, "truePositives": true_positives,
        "falsePositives": false_positives, "falseNegatives": false_negatives,
        "precision": precision, "recall": recall, "hmean": hmean,
        "matches": sorted(matches, key=lambda value: value["expectedIndex"]),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-probe", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--ground-truth-lock", type=Path,
                        default=Path(__file__).resolve().parents[1] / "corpus" / "ground-truth.lock.json")
    arguments = parser.parse_args()
    verified_fixtures = verify_ground_truth(arguments.fixtures.resolve(),
                                            arguments.ground_truth_lock.resolve())
    records = []
    total_characters = 0
    total_errors = 0
    exact_fixtures = 0
    total_expected_lines = 0
    total_observed_lines = 0
    exact_lines = 0
    detection_true_positives = 0
    detection_false_positives = 0
    detection_false_negatives = 0
    model_bundle_id = None
    tag_totals: dict[str, dict[str, int]] = {}
    for fixture in verified_fixtures:
        fixture_path = arguments.fixtures / fixture["id"] / "fixture.json"
        expected = fixture["groundTruth"]["lines"]
        common = [
            "--bundle", str(arguments.bundle),
            "--pixels", str(fixture_path.parent / "pixels.bin"),
            "--width", str(fixture["width"]), "--height", str(fixture["height"]),
            "--stride", str(fixture["stride"]), "--format", fixture["pixelFormat"],
        ]
        process = subprocess.run(
            [str(arguments.native_probe), *common], check=False, capture_output=True,
            text=True, encoding="utf-8"
        )
        if process.returncode != 0:
            raise RuntimeError(f"native probe failed for {fixture['id']}: {process.stdout}{process.stderr}")
        native = json.loads(process.stdout)
        model_bundle_id = model_bundle_id or native["modelBundleId"]
        observed = [line["text"] for line in native["lines"]]
        detection = detection_match(native["boxes"], fixture["groundTruth"]["boxes"])
        detection_true_positives += int(detection["truePositives"])
        detection_false_positives += int(detection["falsePositives"])
        detection_false_negatives += int(detection["falseNegatives"])
        expected_text = "\n".join(expected)
        observed_text = "\n".join(observed)
        errors = edit_distance(expected_text, observed_text)
        total_errors += errors
        total_characters += len(expected_text)
        exact = expected == observed
        exact_fixtures += int(exact)
        total_expected_lines += len(expected)
        total_observed_lines += len(observed)
        exact_line_count = sum(left == right for left, right in zip(expected, observed))
        exact_lines += exact_line_count
        for tag in fixture["tags"]:
            totals = tag_totals.setdefault(tag, {"fixtures": 0, "exactFixtures": 0,
                                                 "characterErrors": 0,
                                                 "referenceCharacters": 0})
            totals["fixtures"] += 1
            totals["exactFixtures"] += int(exact)
            totals["characterErrors"] += errors
            totals["referenceCharacters"] += len(expected_text)
            totals["detectionTruePositives"] = totals.get("detectionTruePositives", 0) + int(detection["truePositives"])
            totals["detectionFalsePositives"] = totals.get("detectionFalsePositives", 0) + int(detection["falsePositives"])
            totals["detectionFalseNegatives"] = totals.get("detectionFalseNegatives", 0) + int(detection["falseNegatives"])
        records.append({
            "fixtureId": fixture["id"], "expected": expected, "observed": observed,
            "exactLineMatch": exact, "characterErrors": errors,
            "referenceCharacters": len(expected_text), "exactLines": exact_line_count,
            "expectedLineCount": len(expected), "observedLineCount": len(observed),
            "pixelSha256": fixture["pixelSha256"], "tags": fixture["tags"],
            "rights": fixture["rights"],
            "groundTruthSource": fixture["groundTruth"]["source"],
            "detection": detection,
        })
    if not records:
        raise RuntimeError("no ground-truth fixtures found")
    by_tag = {}
    for tag, totals in sorted(tag_totals.items()):
        precision = totals["detectionTruePositives"] / (
            totals["detectionTruePositives"] + totals["detectionFalsePositives"]
        ) if totals["detectionTruePositives"] + totals["detectionFalsePositives"] else 1.0
        recall = totals["detectionTruePositives"] / (
            totals["detectionTruePositives"] + totals["detectionFalseNegatives"]
        ) if totals["detectionTruePositives"] + totals["detectionFalseNegatives"] else 1.0
        by_tag[tag] = {
            **totals,
            "exactFixtureAccuracy": totals["exactFixtures"] / totals["fixtures"],
            "characterErrorRate": totals["characterErrors"] / totals["referenceCharacters"]
            if totals["referenceCharacters"] else 0.0,
            "detectionPrecision": precision,
            "detectionRecall": recall,
            "detectionHmean": 2 * precision * recall / (precision + recall)
            if precision + recall else 0.0,
        }
    detection_precision = detection_true_positives / (
        detection_true_positives + detection_false_positives
    ) if detection_true_positives + detection_false_positives else 1.0
    detection_recall = detection_true_positives / (
        detection_true_positives + detection_false_negatives
    ) if detection_true_positives + detection_false_negatives else 1.0
    detection_hmean = 2 * detection_precision * detection_recall / (
        detection_precision + detection_recall
    ) if detection_precision + detection_recall else 0.0
    report = {
        "schemaVersion": "1.0", "baselineEstablished": True,
        "modelBundleId": model_bundle_id,
        "qualityGate": "baseline-only-no-retrospective-threshold",
        "groundTruthScope": "ordered line text and independently maintained quadrilateral text regions for every baseline fixture",
        "groundTruthLockSha256": hashlib.sha256(arguments.ground_truth_lock.read_bytes()).hexdigest(),
        "limitations": [
            "The first bundle report establishes a text, detection, and end-to-end baseline on ten curated fixtures; it is not a general production-accuracy claim.",
            "Generated boxes come from renderer geometry and official-image boxes are project-maintained visible-region annotations; neither is derived from OCR output.",
        ],
        "fixtureCount": len(records), "exactFixtureCount": exact_fixtures,
        "exactFixtureAccuracy": exact_fixtures / len(records),
        "totalExpectedLines": total_expected_lines, "totalObservedLines": total_observed_lines,
        "exactLineCount": exact_lines,
        "exactLineAccuracy": exact_lines / total_expected_lines if total_expected_lines else 1.0,
        "characterErrorRate": total_errors / total_characters if total_characters else 0.0,
        "totalCharacterErrors": total_errors, "totalReferenceCharacters": total_characters,
        "detection": {"iouThreshold": 0.5,
                      "truePositives": detection_true_positives,
                      "falsePositives": detection_false_positives,
                      "falseNegatives": detection_false_negatives,
                      "precision": detection_precision, "recall": detection_recall,
                      "hmean": detection_hmean},
        "byTag": by_tag, "fixtures": records,
    }
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    serialized = json.dumps(report, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"
    arguments.report.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
