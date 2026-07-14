#!/usr/bin/env python3
"""Validate the immutable text and quadrilateral ground-truth contract."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Any


def canonical(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def signed_area(box: list[list[float]]) -> float:
    return sum(
        box[index][0] * box[(index + 1) % 4][1]
        - box[(index + 1) % 4][0] * box[index][1]
        for index in range(4)
    ) / 2.0


def strictly_clockwise_convex(box: list[list[float]]) -> bool:
    for index in range(4):
        point = box[index]
        following = box[(index + 1) % 4]
        after = box[(index + 2) % 4]
        cross = ((following[0] - point[0]) * (after[1] - following[1])
                 - (following[1] - point[1]) * (after[0] - following[0]))
        if not math.isfinite(cross) or cross <= 0:
            return False
    return True


def verify_ground_truth(fixtures: Path, lock_path: Path) -> list[dict[str, Any]]:
    lock = json.loads(lock_path.read_text("utf-8"))
    if lock.get("schemaVersion") != "1.0":
        raise RuntimeError("unsupported ground-truth lock schema")
    locked_records = lock.get("fixtures")
    if not isinstance(locked_records, list) or not locked_records:
        raise RuntimeError("ground-truth lock has no fixtures")
    locked = {record["fixtureId"]: record for record in locked_records}
    if len(locked) != len(locked_records):
        raise RuntimeError("ground-truth lock contains duplicate fixture IDs")
    records: list[dict[str, Any]] = []
    for fixture_path in sorted(fixtures.glob("*/fixture.json")):
        fixture = json.loads(fixture_path.read_text("utf-8"))
        ground_truth = fixture.get("groundTruth")
        if ground_truth is None:
            continue
        fixture_id = fixture["id"]
        if fixture_id not in locked:
            raise RuntimeError(f"ground truth is not locked: {fixture_id}")
        if fixture.get("corpusRevision") != lock.get("revision"):
            raise RuntimeError(f"ground-truth corpus revision mismatch: {fixture_id}")
        pixel_path = fixture_path.parent / "pixels.bin"
        if sha256(pixel_path.read_bytes()) != fixture.get("pixelSha256"):
            raise RuntimeError(f"fixture pixel hash mismatch: {fixture_id}")
        lines = ground_truth.get("lines")
        boxes = ground_truth.get("boxes")
        if not isinstance(lines, list) or not all(isinstance(line, str) for line in lines):
            raise RuntimeError(f"ground-truth lines are invalid: {fixture_id}")
        if not isinstance(boxes, list) or len(boxes) != len(lines):
            raise RuntimeError(f"ground-truth box/line count mismatch: {fixture_id}")
        if not ground_truth.get("source") or not ground_truth.get("annotationPolicy"):
            raise RuntimeError(f"ground-truth provenance is incomplete: {fixture_id}")
        for box in boxes:
            if not isinstance(box, list) or len(box) != 4:
                raise RuntimeError(f"ground-truth box is not a quadrilateral: {fixture_id}")
            for point in box:
                if (
                    not isinstance(point, list)
                    or len(point) != 2
                    or not all(isinstance(value, (int, float)) and math.isfinite(value) for value in point)
                    or point[0] < 0
                    or point[0] > fixture["width"]
                    or point[1] < 0
                    or point[1] > fixture["height"]
                ):
                    raise RuntimeError(f"ground-truth point is invalid: {fixture_id}")
            if signed_area(box) <= 0 or not strictly_clockwise_convex(box):
                raise RuntimeError(
                    f"ground-truth box is degenerate, concave, or not clockwise: {fixture_id}"
                )
        expected = locked[fixture_id]
        if expected["pixelSha256"] != fixture["pixelSha256"]:
            raise RuntimeError(f"ground-truth pixel identity mismatch: {fixture_id}")
        if expected["groundTruthSha256"] != sha256(canonical(ground_truth)):
            raise RuntimeError(f"ground-truth annotation hash mismatch: {fixture_id}")
        records.append(fixture)
    if {fixture["id"] for fixture in records} != set(locked):
        raise RuntimeError("ground-truth lock contains missing or unannotated fixtures")
    return records
