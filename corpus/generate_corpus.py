#!/usr/bin/env python3
"""Materialize immutable raw-pixel corpus fixtures from pinned sources."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import urllib.request

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "corpus" / "sources.lock.json"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def obtain(record: dict[str, object], cache: Path) -> bytes:
    filename = record["name"] if "name" in record else Path(str(record["path"])).name
    destination = cache / str(filename)
    if destination.exists():
        data = destination.read_bytes()
    else:
        request = urllib.request.Request(str(record["url"]), headers={"User-Agent": "light-ocr-corpus/1"})
        with urllib.request.urlopen(request, timeout=300) as response:
            data = response.read()
        temporary = destination.with_suffix(destination.suffix + ".tmp")
        temporary.write_bytes(data)
        os.replace(temporary, destination)
    if len(data) != int(record["bytes"]) or sha256(data) != record["sha256"]:
        raise RuntimeError(f"pinned corpus input mismatch: {destination.name}")
    return data


def write_fixture(
    output: Path,
    fixture_id: str,
    bgr: np.ndarray,
    tags: list[str],
    rights: str,
    provenance: dict[str, object],
    ground_truth: dict[str, object] | None = None,
) -> None:
    directory = output / fixture_id
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    pixels = np.ascontiguousarray(bgr, dtype=np.uint8).tobytes()
    (directory / "pixels.bin").write_bytes(pixels)
    height, width = bgr.shape[:2]
    fixture: dict[str, object] = {
        "schemaVersion": "1.0",
        "id": fixture_id,
        "corpusRevision": "20260714.1",
        "width": width,
        "height": height,
        "stride": width * 3,
        "pixelFormat": "bgr8",
        "pixelSha256": sha256(pixels),
        "rights": rights,
        "tags": tags,
        "provenance": provenance,
    }
    if ground_truth is not None:
        fixture["groundTruth"] = ground_truth
    (directory / "fixture.json").write_text(
        json.dumps(fixture, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )


def quad_from_bounds(left: float, top: float, right: float, bottom: float,
                     width: int, height: int) -> list[list[float]]:
    left = float(max(0.0, min(float(width), float(left))))
    right = float(max(0.0, min(float(width), float(right))))
    top = float(max(0.0, min(float(height), float(top))))
    bottom = float(max(0.0, min(float(height), float(bottom))))
    return [[left, top], [right, top], [right, bottom], [left, bottom]]


def generated_ground_truth(lines: list[str], boxes: list[list[list[float]]]) -> dict[str, object]:
    return {
        "source": "project-generated-layout-v1",
        "annotationPolicy": "visible glyph envelope expanded by 10 pixels before geometric transforms",
        "lines": lines,
        "boxes": boxes,
    }


def render_text(text: str, font_path: Path, size: tuple[int, int], font_size: int,
                foreground: tuple[int, int, int] = (20, 20, 20),
                background: tuple[int, int, int] = (250, 250, 250)) -> tuple[np.ndarray, list[list[float]]]:
    image = Image.new("RGB", size, background)
    font = ImageFont.truetype(str(font_path), font_size)
    draw = ImageDraw.Draw(image)
    box = draw.textbbox((0, 0), text, font=font)
    x = (size[0] - (box[2] - box[0])) // 2
    y = (size[1] - (box[3] - box[1])) // 2 - box[1]
    draw.text((x, y), text, font=font, fill=foreground)
    actual = draw.textbbox((x, y), text, font=font)
    annotation = quad_from_bounds(actual[0] - 10, actual[1] - 10,
                                  actual[2] + 10, actual[3] + 10,
                                  size[0], size[1])
    return cv2.cvtColor(np.asarray(image), cv2.COLOR_RGB2BGR), annotation


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache-dir", type=Path, default=ROOT / ".cache" / "corpus")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "corpus" / "fixtures")
    arguments = parser.parse_args()
    arguments.cache_dir.mkdir(parents=True, exist_ok=True)
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    lock = json.loads(LOCK_PATH.read_text("utf-8"))
    resources = {record["name"]: obtain(record, arguments.cache_dir) for record in lock["resources"]}
    for name, data in resources.items():
        path = arguments.cache_dir / name
        if not path.exists():
            path.write_bytes(data)

    official_rights = (
        "Distributed in PaddleOCR v3.7.0 under the repository Apache-2.0 license; "
        "source path, Git blob, and decoded raw-pixel hash are pinned."
    )
    for record in lock["officialImages"]:
        data = obtain(record, arguments.cache_dir)
        image = cv2.imdecode(np.frombuffer(data, dtype=np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            raise RuntimeError(f"cannot decode official fixture: {record['fixtureId']}")
        write_fixture(
            arguments.output_dir,
            record["fixtureId"],
            image,
            record["tags"],
            official_rights,
            {
                "repository": "https://github.com/PaddlePaddle/PaddleOCR",
                "revision": "v3.7.0",
                "path": record["path"],
                "gitBlob": record["gitBlob"],
                "sourceSha256": record["sha256"],
            },
            record.get("groundTruth"),
        )

    jp_font = arguments.cache_dir / "NotoSansCJKjp-Regular.otf"
    tc_font = arguments.cache_dir / "NotoSansCJKtc-Regular.otf"
    generated_rights = (
        "Generated entirely by project code using Noto Sans CJK under OFL-1.1; "
        "the rendered fixture pixels are redistributable with light-ocr."
    )
    jp_text = "日本語テスト・東京2026"
    jp, jp_box = render_text(jp_text, jp_font, (1100, 190), 82)
    provenance = {"generator": "corpus/generate_corpus.py", "fontRevision": "f8d157532fbfaeda587e826d4cd5b21a49186f7c"}
    write_fixture(arguments.output_dir, "generated-japanese-horizontal", jp,
                  ["japanese", "digits", "punctuation", "horizontal", "single-line"],
                  generated_rights, provenance, generated_ground_truth([jp_text], [jp_box]))
    rotated_jp_box = [[190 - jp_box[3][1], jp_box[3][0]],
                      [190 - jp_box[0][1], jp_box[0][0]],
                      [190 - jp_box[1][1], jp_box[1][0]],
                      [190 - jp_box[2][1], jp_box[2][0]]]
    write_fixture(arguments.output_dir, "generated-japanese-rotated", cv2.rotate(jp, cv2.ROTATE_90_CLOCKWISE),
                  ["japanese", "digits", "punctuation", "vertical", "rotated-box", "single-line"],
                  generated_rights, provenance,
                  generated_ground_truth([jp_text], [rotated_jp_box]))

    tc_text = "繁體中文測試：臺灣・OCR 2026"
    tc, tc_box = render_text(tc_text, tc_font, (1280, 190), 78)
    write_fixture(arguments.output_dir, "generated-traditional-horizontal", tc,
                  ["traditional-chinese", "english", "digits", "punctuation", "mixed-script", "horizontal"],
                  generated_rights, provenance, generated_ground_truth([tc_text], [tc_box]))

    low, low_box = render_text("LOW CONTRAST 2026", jp_font, (1000, 220), 74,
                               foreground=(145, 145, 145), background=(215, 215, 215))
    gradient = np.linspace(0, 22, low.shape[1], dtype=np.uint8)[None, :, None]
    low = np.clip(low.astype(np.uint16) + gradient, 0, 255).astype(np.uint8)
    source = np.float32([[0, 0], [999, 0], [999, 219], [0, 219]])
    destination = np.float32([[35, 18], [970, 0], [999, 200], [0, 219]])
    transform = cv2.getPerspectiveTransform(source, destination)
    low = cv2.warpPerspective(low, transform, (1000, 220),
                              flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE)
    transformed_low_box = cv2.perspectiveTransform(
        np.asarray([low_box], dtype=np.float32), transform
    )[0].astype(float).tolist()
    write_fixture(arguments.output_dir, "generated-low-contrast-perspective", low,
                  ["english", "digits", "low-contrast", "uneven-illumination", "perspective", "single-line"],
                  generated_rights, provenance,
                  generated_ground_truth(["LOW CONTRAST 2026"], [transformed_low_box]))

    hello = np.full((180, 800, 3), 255, dtype=np.uint8)
    cv2.putText(hello, "HELLO 123", (35, 125), cv2.FONT_HERSHEY_SIMPLEX, 2.5,
                (0, 0, 0), 5, cv2.LINE_AA)
    rows, columns = np.where(np.any(hello < 250, axis=2))
    hello_box = quad_from_bounds(columns.min() - 10, rows.min() - 10,
                                 columns.max() + 11, rows.max() + 11, 800, 180)
    write_fixture(arguments.output_dir, "generated-hello-123", hello,
                  ["english", "digits", "punctuation", "sparse", "single-line", "synthetic"],
                  "Generated entirely by project code; redistributable with light-ocr.",
                  {"generator": "corpus/generate_corpus.py",
                   "renderer": "OpenCV Hershey FONT_HERSHEY_SIMPLEX"},
                  generated_ground_truth(["HELLO 123"], [hello_box]))

    blank = np.full((160, 320, 3), 255, dtype=np.uint8)
    write_fixture(arguments.output_dir, "generated-blank", blank,
                  ["blank", "no-text", "sparse"], generated_rights,
                  {"generator": "corpus/generate_corpus.py"},
                  generated_ground_truth([], []))
    expected_ids = {record["fixtureId"] for record in lock["officialImages"]} | {
        "generated-japanese-horizontal", "generated-japanese-rotated",
        "generated-traditional-horizontal", "generated-low-contrast-perspective",
        "generated-hello-123", "generated-blank",
    }
    actual_ids = {path.name for path in arguments.output_dir.iterdir() if path.is_dir()}
    if actual_ids != expected_ids:
        raise RuntimeError(
            f"fixture directory set mismatch: expected {sorted(expected_ids)}, got {sorted(actual_ids)}"
        )
    print(arguments.output_dir.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
