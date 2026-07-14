#!/usr/bin/env python3
"""Compare a native stage-probe record with the pinned Python oracle record."""

from __future__ import annotations

import argparse
import base64
import json
import math
from pathlib import Path
from typing import Any


def polygon_area(points: list[list[float]]) -> float:
    if len(points) < 3:
        return 0.0
    return abs(
        sum(
            points[index][0] * points[(index + 1) % len(points)][1]
            - points[(index + 1) % len(points)][0] * points[index][1]
            for index in range(len(points))
        )
    ) / 2.0


def polygon_clip(subject: list[list[float]], clip: list[list[float]]) -> list[list[float]]:
    def inside(point: list[float], left: list[float], right: list[float]) -> bool:
        return (right[0] - left[0]) * (point[1] - left[1]) - (right[1] - left[1]) * (point[0] - left[0]) >= 0

    def intersection(start: list[float], end: list[float], left: list[float], right: list[float]) -> list[float]:
        x1, y1 = start
        x2, y2 = end
        x3, y3 = left
        x4, y4 = right
        denominator = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4)
        if denominator == 0:
            return end
        first = x1 * y2 - y1 * x2
        second = x3 * y4 - y3 * x4
        return [
            (first * (x3 - x4) - (x1 - x2) * second) / denominator,
            (first * (y3 - y4) - (y1 - y2) * second) / denominator,
        ]

    output = subject
    for index in range(len(clip)):
        input_points = output
        output = []
        if not input_points:
            break
        left = clip[index]
        right = clip[(index + 1) % len(clip)]
        start = input_points[-1]
        for end in input_points:
            if inside(end, left, right):
                if not inside(start, left, right):
                    output.append(intersection(start, end, left, right))
                output.append(end)
            elif inside(start, left, right):
                output.append(intersection(start, end, left, right))
            start = end
    return output


def box_metrics(native: list[list[float]], oracle: list[list[float]]) -> tuple[float, float]:
    intersection = polygon_area(polygon_clip(native, oracle))
    union = polygon_area(native) + polygon_area(oracle) - intersection
    iou = intersection / union if union > 0 else 0.0
    maximum_corner = max(
        math.hypot(native[index][0] - oracle[index][0], native[index][1] - oracle[index][1])
        for index in range(4)
    )
    return iou, maximum_corner


def maximum_point_difference(left: Any, right: Any) -> float | None:
    if left is None or right is None:
        return 0.0 if left is right else None
    if len(left) != len(right):
        return None
    return max(
        (
            math.hypot(
                left_point[0] - right_point[0],
                left_point[1] - right_point[1],
            )
            for left_point, right_point in zip(left, right)
        ),
        default=0.0,
    )


def compare(native: dict[str, Any], oracle: dict[str, Any],
            exceptions: list[dict[str, Any]] | None = None) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    exception_by_checkpoint = {
        record["checkpoint"]: record for record in (exceptions or [])
    }
    applied_exception_ids: list[str] = []

    def exact(name: str, left: Any, right: Any) -> None:
        checks.append({"checkpoint": name, "passed": left == right, "native": left, "oracle": right})

    exact("modelBundleId", native["modelBundleId"], oracle["modelBundleId"])
    exact("image", native["image"], oracle["image"])
    exact("models", native["models"], oracle["models"])
    for stage in ("detectionInput", "detectionOutput"):
        exact(f"{stage}.shape", native[stage]["shape"], oracle[stage]["shape"])
        exact(f"{stage}.sha256", native[stage]["sha256Float32LE"], oracle[stage]["sha256Float32LE"])
    exact("contourCandidates", native["contourCandidates"], oracle["contourCandidates"])
    exact("thresholdBitmapSha256", native["thresholdBitmapSha256"],
          oracle["thresholdBitmapSha256"])
    exact("detectionCandidateCount", len(native["detectionCandidates"]),
          len(oracle["detectionCandidates"]))
    for index, (native_candidate, oracle_candidate) in enumerate(
        zip(native["detectionCandidates"], oracle["detectionCandidates"])
    ):
        exact(f"detectionCandidates[{index}].candidateIndex",
              native_candidate["candidateIndex"], oracle_candidate["candidateIndex"])
        exact(f"detectionCandidates[{index}].decision",
              native_candidate["decision"], oracle_candidate["decision"])
        native_score = native_candidate["score"]
        oracle_score = oracle_candidate["score"]
        score_difference = (
            0.0
            if native_score is None and oracle_score is None
            else abs(native_score - oracle_score)
            if native_score is not None and oracle_score is not None
            else math.inf
        )
        checkpoint = f"detectionCandidates[{index}].score"
        exception = exception_by_checkpoint.get(checkpoint)
        tolerance = (
            float(exception["tolerance"]["absoluteDifference"])
            if exception is not None
            else 1e-5
        )
        invariant_passed = (
            exception is None
            or native_candidate["decision"] == oracle_candidate["decision"]
            == "below_box_threshold"
        )
        passed = score_difference <= tolerance and invariant_passed
        if passed and exception is not None:
            applied_exception_ids.append(exception["id"])
        checks.append({
            "checkpoint": checkpoint,
            "passed": passed,
            "absoluteDifference": score_difference,
            "tolerance": tolerance,
            **({"exceptionId": exception["id"]} if exception is not None else {}),
        })
        for field in ("initialQuad", "expandedPolygon", "expandedQuad", "restoredQuad"):
            difference = maximum_point_difference(native_candidate[field], oracle_candidate[field])
            checks.append({
                "checkpoint": f"detectionCandidates[{index}].{field}",
                "passed": difference is not None and difference <= 0.01,
                "maximumPointDifference": difference,
                "tolerance": 0.01,
            })
    exact("lineCount", len(native["lines"]), len(oracle["lines"]))
    exact("cropCount", len(native["crops"]), len(oracle["crops"]))
    exact("recognitionBatchCount", len(native["recognitionBatches"]), len(oracle["recognitionBatches"]))

    if len(native["boxes"]) == len(oracle["boxes"]):
        for index, (native_box, oracle_box) in enumerate(zip(native["boxes"], oracle["boxes"])):
            iou, corner = box_metrics(native_box, oracle_box)
            checks.append({"checkpoint": f"boxes[{index}]", "passed": iou >= 0.98 and corner <= 2.0, "iou": iou, "maximumCornerDistance": corner})
    else:
        checks.append({"checkpoint": "boxCount", "passed": False, "native": len(native["boxes"]), "oracle": len(oracle["boxes"])})

    for index, (native_crop, oracle_crop) in enumerate(zip(native["crops"], oracle["crops"])):
        exact(f"crops[{index}].shape", [native_crop["height"], native_crop["width"], native_crop["channels"]], [oracle_crop["height"], oracle_crop["width"], oracle_crop["channels"]])
        hashes_match = native_crop["sha256Bgr8"] == oracle_crop["sha256Bgr8"]
        if hashes_match:
            checks.append({"checkpoint": f"crops[{index}].pixels", "passed": True, "exactHash": True})
        else:
            native_pixels = base64.b64decode(native_crop["pixelsBgr8Base64"], validate=True)
            oracle_pixels = base64.b64decode(oracle_crop["pixelsBgr8Base64"], validate=True)
            if len(native_pixels) != len(oracle_pixels):
                checks.append({"checkpoint": f"crops[{index}].pixels", "passed": False, "exactHash": False, "nativeBytes": len(native_pixels), "oracleBytes": len(oracle_pixels)})
            else:
                differences = [abs(left - right) for left, right in zip(native_pixels, oracle_pixels)]
                maximum = max(differences, default=0)
                mean = sum(differences) / len(differences) if differences else 0.0
                checks.append({"checkpoint": f"crops[{index}].pixels", "passed": maximum <= 3 and mean <= 0.05, "exactHash": False, "maximumAbsoluteDifference": maximum, "meanAbsoluteDifference": mean, "differingValues": sum(value != 0 for value in differences), "maximumAllowed": 3, "meanAllowed": 0.05})

    for index, (native_batch, oracle_batch) in enumerate(zip(native["recognitionBatches"], oracle["recognitionBatches"])):
        for field in ("inputIndices", "inputShape", "outputShape"):
            exact(f"recognitionBatches[{index}].{field}", native_batch[field], oracle_batch[field])
        for name, hash_field, samples_field, tolerance in (
            ("input", "inputSha256Float32LE", "inputSamples", 0.02),
            ("output", "outputSha256Float32LE", "outputSamples", 0.02),
        ):
            hashes_match = native_batch[hash_field] == oracle_batch[hash_field]
            native_samples = native_batch[samples_field]
            oracle_samples = oracle_batch[samples_field]
            sample_indices_match = [value["index"] for value in native_samples] == [value["index"] for value in oracle_samples]
            maximum = max(
                (abs(left["value"] - right["value"]) for left, right in zip(native_samples, oracle_samples)),
                default=0.0,
            )
            checks.append({"checkpoint": f"recognitionBatches[{index}].{name}Values", "passed": hashes_match or (sample_indices_match and maximum <= tolerance), "exactHash": hashes_match, "maximumSampleDifference": maximum, "tolerance": tolerance})

    for index, (native_decoded, oracle_decoded) in enumerate(zip(native["decoded"], oracle["decoded"])):
        exact(f"decoded[{index}].indices", native_decoded["selectedIndices"], oracle_decoded["selectedIndices"])
        exact(f"decoded[{index}].text", native_decoded["text"], oracle_decoded["text"])
        difference = abs(native_decoded["confidence"] - oracle_decoded["confidence"])
        checks.append({"checkpoint": f"decoded[{index}].confidence", "passed": difference <= 0.001, "absoluteDifference": difference})

    exact("final.text", [line["text"] for line in native["lines"]], [line["text"] for line in oracle["lines"]])
    return {"schemaVersion": "1.0", "passed": all(check["passed"] for check in checks),
            "appliedExceptionIds": sorted(set(applied_exception_ids)), "checks": checks}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    arguments = parser.parse_args()
    report = compare(json.loads(arguments.native.read_text("utf-8")), json.loads(arguments.oracle.read_text("utf-8")))
    serialized = json.dumps(report, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"
    if arguments.report:
        arguments.report.parent.mkdir(parents=True, exist_ok=True)
        arguments.report.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
