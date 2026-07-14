#!/usr/bin/env python3
"""Benchmark the pinned Python PP-OCRv6 oracle with persistent sessions."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import resource
import sys
import time
from typing import Any

import numpy as np
import psutil

from oracle import (
    crop_text,
    db_postprocess,
    decode,
    detection_input,
    load_raw_bytes,
    recognition_batches,
    session,
    sort_boxes,
)


def elapsed_us(begin: int, end: int) -> int:
    return (end - begin) // 1000


def distribution(values: list[int]) -> dict[str, int]:
    ordered = sorted(values)

    def percentile(value: float) -> int:
        return ordered[min(len(ordered) - 1, math.ceil(value * len(ordered)) - 1)]

    return {
        "minimum": ordered[0],
        "median": percentile(0.5),
        "p95": percentile(0.95),
        "maximum": ordered[-1],
    }


def benchmark(
    bundle: Path,
    pixels: Path,
    width: int,
    height: int,
    stride: int,
    pixel_format: str,
    warmup: int,
    iterations: int,
) -> dict[str, Any]:
    load_begin = time.perf_counter_ns()
    manifest = json.loads((bundle / "manifest.json").read_text("utf-8"))
    config = json.loads((bundle / manifest["normalizedConfigPath"]).read_text("utf-8"))
    characters = json.loads(
        (bundle / config["recognition"]["decode"]["dictionaryPath"]).read_text("utf-8")
    )["characters"]
    raw_pixels = pixels.read_bytes()
    model_bundle_bytes = sum(path.stat().st_size for path in bundle.rglob("*") if path.is_file())
    load_end = time.perf_counter_ns()

    initialize_begin = time.perf_counter_ns()
    det_session = session(bundle / manifest["models"]["detection"]["modelPath"])
    rec_session = session(bundle / manifest["models"]["recognition"]["modelPath"])
    initialize_end = time.perf_counter_ns()

    names = [
        "inputValidation",
        "detectionPreprocess",
        "detectionInference",
        "detectionPostprocess",
        "cropAndSort",
        "recognitionPreprocess",
        "recognitionInference",
        "recognitionPostprocess",
    ]
    stage_samples: dict[str, list[int]] = {name: [] for name in names}
    wall_samples: list[int] = []
    inference_samples: list[int] = []
    resident_samples: list[int] = []

    def run_once(record: bool) -> None:
        wall_begin = time.perf_counter_ns()
        stage_begin = wall_begin
        image = load_raw_bytes(raw_pixels, width, height, stride, pixel_format)
        stage_end = time.perf_counter_ns()
        timings = [elapsed_us(stage_begin, stage_end)]

        stage_begin = stage_end
        det_input = detection_input(image, config["detection"])
        stage_end = time.perf_counter_ns()
        timings.append(elapsed_us(stage_begin, stage_end))

        stage_begin = stage_end
        det_output = np.asarray(
            det_session.run(None, {det_session.get_inputs()[0].name: det_input})[0],
            dtype=np.float32,
        )
        stage_end = time.perf_counter_ns()
        timings.append(elapsed_us(stage_begin, stage_end))

        stage_begin = stage_end
        probability = det_output[0, 0] if det_output.ndim == 4 else det_output[0]
        _, boxes, _, _ = db_postprocess(probability, width, height, config["detection"])
        stage_end = time.perf_counter_ns()
        timings.append(elapsed_us(stage_begin, stage_end))

        stage_begin = stage_end
        boxes = sort_boxes(boxes, config["geometry"]["rowBandPixels"])
        crops = [crop_text(image, box, config["geometry"]) for box in boxes]
        stage_end = time.perf_counter_ns()
        timings.append(elapsed_us(stage_begin, stage_end))

        stage_begin = stage_end
        batches = recognition_batches(crops, config["recognition"])
        stage_end = time.perf_counter_ns()
        timings.append(elapsed_us(stage_begin, stage_end))

        stage_begin = stage_end
        outputs: list[tuple[list[int], np.ndarray]] = []
        for indices, rec_input in batches:
            output = np.asarray(
                rec_session.run(None, {rec_session.get_inputs()[0].name: rec_input})[0],
                dtype=np.float32,
            )
            outputs.append((indices, output))
        stage_end = time.perf_counter_ns()
        timings.append(elapsed_us(stage_begin, stage_end))

        stage_begin = stage_end
        decoded: list[dict[str, Any]] = [{} for _ in boxes]
        for indices, output in outputs:
            for source_index, decoded_value in zip(indices, decode(output, characters)):
                decoded[source_index] = decoded_value
        threshold = config["recognition"]["defaultScoreThreshold"]
        lines = [
            value["text"]
            for value in decoded
            if value["text"] and value["confidence"] >= threshold
        ]
        if boxes and not lines:
            raise RuntimeError("oracle benchmark decoded no accepted text")
        stage_end = time.perf_counter_ns()
        timings.append(elapsed_us(stage_begin, stage_end))
        wall_end = stage_end

        if record:
            wall_samples.append(elapsed_us(wall_begin, wall_end))
            inference_samples.append(timings[2] + timings[6])
            for name, value in zip(names, timings):
                stage_samples[name].append(value)
            resident_samples.append(psutil.Process().memory_info().rss)

    for _ in range(warmup):
        run_once(False)
    for _ in range(iterations):
        run_once(True)

    peak_rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if sys.platform != "darwin":
        peak_rss *= 1024

    return {
        "schemaVersion": "1.0",
        "ok": True,
        "backend": "python-oracle",
        "modelBundleId": manifest["bundleId"],
        "modelBundleBytes": model_bundle_bytes,
        "loadUs": elapsed_us(load_begin, load_end),
        "engineInitializationUs": elapsed_us(initialize_begin, initialize_end),
        "warmup": warmup,
        "iterations": iterations,
        "latencyUs": distribution(wall_samples),
        "reportedTotalUs": distribution(wall_samples),
        "inferenceOnlyUs": distribution(inference_samples),
        "stagesUs": {name: distribution(values) for name, values in stage_samples.items()},
        "memoryBytes": {
            "residentMinimum": min(resident_samples),
            "residentMaximum": max(resident_samples),
            "residentFinal": resident_samples[-1],
            "peakResident": peak_rss,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--pixels", type=Path, required=True)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--stride", type=int, required=True)
    parser.add_argument("--format", choices=["gray8", "rgb8", "bgr8", "rgba8"], required=True)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--iterations", type=int, default=30)
    arguments = parser.parse_args()
    report = benchmark(
        arguments.bundle,
        arguments.pixels,
        arguments.width,
        arguments.height,
        arguments.stride,
        arguments.format,
        arguments.warmup,
        arguments.iterations,
    )
    print(json.dumps(report, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
