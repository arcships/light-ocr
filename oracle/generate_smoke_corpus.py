#!/usr/bin/env python3
"""Generate the deterministic, redistributable smoke parity fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import cv2
import numpy as np


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output", type=Path, default=Path("corpus/fixtures/generated-hello-123")
    )
    arguments = parser.parse_args()
    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    image = np.full((180, 800, 3), 255, dtype=np.uint8)
    cv2.putText(
        image,
        "HELLO 123",
        (35, 125),
        cv2.FONT_HERSHEY_SIMPLEX,
        2.5,
        (0, 0, 0),
        5,
        cv2.LINE_AA,
    )
    rows, columns = np.where(np.any(image < 250, axis=2))
    left = float(columns.min() - 10)
    right = float(columns.max() + 11)
    top = float(rows.min() - 10)
    bottom = float(rows.max() + 11)
    pixels = image.tobytes()
    (output / "pixels.bin").write_bytes(pixels)
    fixture = {
        "schemaVersion": "1.0",
        "id": "generated-hello-123",
        "corpusRevision": "20260714.1",
        "width": 800,
        "height": 180,
        "stride": 2400,
        "pixelFormat": "bgr8",
        "pixelSha256": hashlib.sha256(pixels).hexdigest(),
        "provenance": {
            "generator": "corpus/generate_corpus.py",
            "renderer": "OpenCV Hershey FONT_HERSHEY_SIMPLEX",
        },
        "groundTruth": {
            "source": "project-generated-layout-v1",
            "annotationPolicy": "visible glyph envelope expanded by 10 pixels before geometric transforms",
            "lines": ["HELLO 123"],
            "boxes": [[[left, top], [right, top], [right, bottom], [left, bottom]]],
        },
        "rights": "Generated entirely by project code; redistributable with light-ocr.",
        "tags": ["english", "digits", "punctuation", "sparse", "single-line", "synthetic"],
    }
    (output / "fixture.json").write_text(
        json.dumps(fixture, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
