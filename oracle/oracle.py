#!/usr/bin/env python3
"""Pinned test-only PP-OCRv6 ONNX stage oracle for raw pixel fixtures."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
from pathlib import Path
from typing import Any

import cv2
import numpy as np
import onnxruntime as ort
import pyclipper


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def samples(values: np.ndarray) -> list[dict[str, int | float]]:
    flat = np.ascontiguousarray(values, dtype="<f4").reshape(-1)
    if flat.size == 0:
        return []
    indices = sorted({0, flat.size // 4, flat.size // 2, flat.size * 3 // 4, flat.size - 1})
    return [{"index": index, "value": float(flat[index])} for index in indices]


def tensor_record(values: np.ndarray) -> dict[str, Any]:
    packed = np.ascontiguousarray(values, dtype="<f4")
    return {
        "shape": list(packed.shape),
        "sha256Float32LE": sha256(packed.tobytes()),
        "samples": samples(packed),
    }


def load_raw_bytes(data: bytes, width: int, height: int, stride: int, pixel_format: str) -> np.ndarray:
    channels = {"gray8": 1, "rgb8": 3, "bgr8": 3, "rgba8": 4}[pixel_format]
    row_bytes = width * channels
    required = (height - 1) * stride + row_bytes
    if stride < row_bytes or len(data) < required:
        raise ValueError("raw fixture is truncated or has an invalid stride")
    if channels == 1:
        view = np.ndarray((height, width), dtype=np.uint8, buffer=data, strides=(stride, 1))
        packed = np.ascontiguousarray(view)
    else:
        view = np.ndarray(
            (height, width, channels),
            dtype=np.uint8,
            buffer=data,
            strides=(stride, channels, 1),
        )
        packed = np.ascontiguousarray(view)
    if pixel_format == "gray8":
        return cv2.cvtColor(packed, cv2.COLOR_GRAY2BGR)
    if pixel_format == "rgb8":
        return cv2.cvtColor(packed, cv2.COLOR_RGB2BGR)
    if pixel_format == "rgba8":
        return cv2.cvtColor(packed, cv2.COLOR_RGBA2BGR)
    return packed.copy()


def load_raw(path: Path, width: int, height: int, stride: int, pixel_format: str) -> np.ndarray:
    return load_raw_bytes(path.read_bytes(), width, height, stride, pixel_format)


def detection_input(image: np.ndarray, config: dict[str, Any]) -> np.ndarray:
    source = image
    height, width = image.shape[:2]
    if height + width < 64:
        source = np.zeros((max(32, height), max(32, width), 3), dtype=np.uint8)
        source[:height, :width] = image
    height, width = source.shape[:2]
    resize = config["resize"]
    ratio = resize["limitSideLen"] / min(height, width) if min(height, width) < resize["limitSideLen"] else 1.0
    resized_height = int(height * ratio)
    resized_width = int(width * ratio)
    if max(resized_height, resized_width) > resize["maxSideLimit"]:
        ratio = resize["maxSideLimit"] / max(resized_height, resized_width)
        resized_height = int(resized_height * ratio)
        resized_width = int(resized_width * ratio)
    multiple = resize["dimensionMultiple"]
    resized_height = max(round(resized_height / multiple) * multiple, resize["minimumDimension"])
    resized_width = max(round(resized_width / multiple) * multiple, resize["minimumDimension"])
    resized = cv2.resize(source, (resized_width, resized_height), interpolation=cv2.INTER_LINEAR)
    normalize = config["normalize"]
    tensor = resized.astype(np.float32) * np.float32(normalize["scale"])
    tensor = (tensor - np.asarray(normalize["mean"], dtype=np.float32)) / np.asarray(
        normalize["std"], dtype=np.float32
    )
    return np.ascontiguousarray(tensor.transpose(2, 0, 1)[None], dtype=np.float32)


def order_quad(points: np.ndarray) -> np.ndarray:
    ordered_x = sorted(points.tolist(), key=lambda point: (point[0], point[1]))
    left_top, left_bottom = (ordered_x[0], ordered_x[1]) if ordered_x[1][1] > ordered_x[0][1] else (ordered_x[1], ordered_x[0])
    right_top, right_bottom = (ordered_x[2], ordered_x[3]) if ordered_x[3][1] > ordered_x[2][1] else (ordered_x[3], ordered_x[2])
    return np.asarray([left_top, right_top, right_bottom, left_bottom], dtype=np.float32)


def box_score_fast(probability: np.ndarray, box: np.ndarray) -> float:
    height, width = probability.shape
    xmin = int(np.clip(np.floor(box[:, 0].min()), 0, width - 1))
    xmax = int(np.clip(np.ceil(box[:, 0].max()), 0, width - 1))
    ymin = int(np.clip(np.floor(box[:, 1].min()), 0, height - 1))
    ymax = int(np.clip(np.ceil(box[:, 1].max()), 0, height - 1))
    mask = np.zeros((ymax - ymin + 1, xmax - xmin + 1), dtype=np.uint8)
    relative = box.copy()
    relative[:, 0] -= xmin
    relative[:, 1] -= ymin
    cv2.fillPoly(mask, relative.reshape(1, -1, 2).astype(np.int32), 1)
    return float(cv2.mean(probability[ymin : ymax + 1, xmin : xmax + 1], mask)[0])


def unclip(box: np.ndarray, ratio: float) -> np.ndarray | None:
    x = box[:, 0].astype(np.float64)
    y = box[:, 1].astype(np.float64)
    area = abs(float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))) / 2.0
    perimeter = float(np.hypot(np.roll(x, -1) - x, np.roll(y, -1) - y).sum())
    if area <= 0 or perimeter <= 0:
        return None
    offset = pyclipper.PyclipperOffset()
    offset.AddPath(box.tolist(), pyclipper.JT_ROUND, pyclipper.ET_CLOSEDPOLYGON)
    expanded = offset.Execute(area * ratio / perimeter)
    if len(expanded) != 1 or len(expanded[0]) < 3:
        return None
    return np.asarray(expanded[0], dtype=np.float32)


def db_postprocess(probability: np.ndarray, original_width: int, original_height: int,
                   config: dict[str, Any]) -> tuple[int, list[np.ndarray], str, list[dict[str, Any]]]:
    post = config["postprocess"]
    bitmap = (probability > np.float32(post["threshold"])).astype(np.uint8) * 255
    if post["useDilation"]:
        bitmap = cv2.dilate(bitmap, np.ones((2, 2), dtype=np.uint8))
    contours, _ = cv2.findContours(bitmap, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
    candidates = min(len(contours), post["maxCandidates"])
    height, width = probability.shape
    boxes: list[np.ndarray] = []
    traces: list[dict[str, Any]] = []
    for candidate_index, contour in enumerate(contours[:candidates]):
        rectangle = cv2.minAreaRect(contour)
        box = order_quad(cv2.boxPoints(rectangle))
        trace: dict[str, Any] = {
            "candidateIndex": candidate_index,
            "initialQuad": box.tolist(),
            "score": None,
            "expandedPolygon": [],
            "expandedQuad": None,
            "restoredQuad": None,
            "decision": "",
        }
        if min(rectangle[1]) < post["minimumBoxSide"]:
            trace["decision"] = "initial_side_too_small"
            traces.append(trace)
            continue
        score = box_score_fast(probability, box)
        trace["score"] = score
        if score < post["boxThreshold"]:
            trace["decision"] = "below_box_threshold"
            traces.append(trace)
            continue
        expanded = unclip(box, post["unclipRatio"])
        if expanded is None:
            trace["decision"] = "unclip_failed"
            traces.append(trace)
            continue
        trace["expandedPolygon"] = expanded.tolist()
        expanded_rectangle = cv2.minAreaRect(expanded)
        if min(expanded_rectangle[1]) < post["minimumBoxSide"] + 2:
            trace["decision"] = "expanded_side_too_small"
            traces.append(trace)
            continue
        expanded_box = order_quad(cv2.boxPoints(expanded_rectangle))
        trace["expandedQuad"] = expanded_box.tolist()
        expanded_box[:, 0] = np.clip(np.round(expanded_box[:, 0] / width * original_width), 0, original_width)
        expanded_box[:, 1] = np.clip(np.round(expanded_box[:, 1] / height * original_height), 0, original_height)
        trace["restoredQuad"] = expanded_box.tolist()
        width_value = np.linalg.norm(expanded_box[1] - expanded_box[0])
        height_value = np.linalg.norm(expanded_box[3] - expanded_box[0])
        if width_value <= post["minimumBoxSide"] or height_value <= post["minimumBoxSide"]:
            trace["decision"] = "restored_side_too_small"
            traces.append(trace)
            continue
        boxes.append(expanded_box.astype(np.float32))
        trace["decision"] = "accepted"
        traces.append(trace)
    return candidates, boxes, sha256(bitmap.tobytes()), traces


def sort_boxes(boxes: list[np.ndarray], row_band: int) -> list[np.ndarray]:
    result = sorted(boxes, key=lambda box: (box[0][1], box[0][0]))
    for index in range(len(result) - 1):
        for current in range(index, -1, -1):
            if abs(result[current + 1][0][1] - result[current][0][1]) < row_band and result[current + 1][0][0] < result[current][0][0]:
                result[current], result[current + 1] = result[current + 1], result[current]
            else:
                break
    return result


def crop_text(image: np.ndarray, box: np.ndarray, geometry: dict[str, Any]) -> np.ndarray:
    width = int(max(np.linalg.norm(box[0] - box[1]), np.linalg.norm(box[2] - box[3])))
    height = int(max(np.linalg.norm(box[0] - box[3]), np.linalg.norm(box[1] - box[2])))
    if width <= 0 or height <= 0:
        raise ValueError("box produces an empty crop")
    destination = np.asarray([[0, 0], [width, 0], [width, height], [0, height]], dtype=np.float32)
    transform = cv2.getPerspectiveTransform(box.astype(np.float32), destination)
    crop = cv2.warpPerspective(image, transform, (width, height), flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE)
    if crop.shape[0] / crop.shape[1] >= geometry["tallLineRatio"]:
        crop = np.rot90(crop)
    return np.ascontiguousarray(crop)


def recognition_batches(crops: list[np.ndarray], config: dict[str, Any]) -> list[tuple[list[int], np.ndarray]]:
    input_config = config["input"]
    normalize = config["normalize"]
    batch_size = config["batch"]["defaultSize"]
    height = input_config["shape"][1]
    base_width = input_config["shape"][2]
    base_ratio = base_width / height
    sample_widths = []
    for crop in crops:
        ratio = crop.shape[1] / crop.shape[0]
        width = int(height * max(base_ratio, ratio))
        sample_widths.append(max(input_config["minimumTensorWidth"], min(input_config["maximumTensorWidth"], width)))
    order = sorted(range(len(crops)), key=lambda index: sample_widths[index])
    result: list[tuple[list[int], np.ndarray]] = []
    for begin in range(0, len(order), batch_size):
        indices = order[begin : begin + batch_size]
        tensor_width = max(sample_widths[index] for index in indices)
        tensor = np.full((len(indices), 3, height, tensor_width), np.float32(normalize["paddingValue"]), dtype=np.float32)
        for batch_index, source_index in enumerate(indices):
            crop = crops[source_index]
            ratio = crop.shape[1] / crop.shape[0]
            content_width = min(sample_widths[source_index], math.ceil(height * ratio))
            resized = cv2.resize(crop, (content_width, height), interpolation=cv2.INTER_LINEAR).astype(np.float32)
            resized = resized * np.float32(normalize["scale"])
            resized = (resized - np.asarray(normalize["mean"], dtype=np.float32)) / np.asarray(normalize["std"], dtype=np.float32)
            tensor[batch_index, :, :, :content_width] = resized.transpose(2, 0, 1)
        result.append((indices, np.ascontiguousarray(tensor)))
    return result


def decode(output: np.ndarray, characters: list[str]) -> list[dict[str, Any]]:
    best_indices = output.argmax(axis=2)
    best_probabilities = output.max(axis=2)
    results = []
    for indices, probabilities in zip(best_indices, best_probabilities):
        selected_indices: list[int] = []
        selected_probabilities: list[float] = []
        text: list[str] = []
        previous = None
        for index, probability in zip(indices.tolist(), probabilities.tolist()):
            if index != 0 and index != previous:
                selected_indices.append(index)
                selected_probabilities.append(probability)
                text.append(characters[index - 1])
            previous = index
        confidence = float(np.mean(np.asarray(selected_probabilities, dtype=np.float64))) if selected_probabilities else 0.0
        results.append({"text": "".join(text), "confidence": confidence, "selectedIndices": selected_indices, "selectedProbabilities": selected_probabilities})
    return results


def session(path: Path) -> ort.InferenceSession:
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    return ort.InferenceSession(path, sess_options=options, providers=["CPUExecutionProvider"])


def run(bundle: Path, pixels: Path, width: int, height: int, stride: int, pixel_format: str,
        include_crop_pixels: bool = False) -> dict[str, Any]:
    manifest = json.loads((bundle / "manifest.json").read_text("utf-8"))
    config = json.loads((bundle / manifest["normalizedConfigPath"]).read_text("utf-8"))
    characters = json.loads((bundle / config["recognition"]["decode"]["dictionaryPath"]).read_text("utf-8"))["characters"]
    image = load_raw(pixels, width, height, stride, pixel_format)
    det_input = detection_input(image, config["detection"])
    det_session = session(bundle / manifest["models"]["detection"]["modelPath"])
    rec_session = session(bundle / manifest["models"]["recognition"]["modelPath"])
    det_output = np.asarray(det_session.run(None, {det_session.get_inputs()[0].name: det_input})[0], dtype=np.float32)
    probability = det_output[0, 0] if det_output.ndim == 4 else det_output[0]
    candidate_count, boxes, bitmap_sha256, detection_candidates = db_postprocess(
        probability, width, height, config["detection"]
    )
    boxes = sort_boxes(boxes, config["geometry"]["rowBandPixels"])
    crops = [crop_text(image, box, config["geometry"]) for box in boxes]

    result: dict[str, Any] = {
        "schemaVersion": "1.0",
        "modelBundleId": manifest["bundleId"],
        "image": {"width": width, "height": height},
        "models": {
            "detection": {
                "inputName": det_session.get_inputs()[0].name,
                "outputName": det_session.get_outputs()[0].name,
            },
            "recognition": {
                "inputName": rec_session.get_inputs()[0].name,
                "outputName": rec_session.get_outputs()[0].name,
            },
        },
        "detectionInput": tensor_record(det_input),
        "detectionOutput": tensor_record(det_output),
        "contourCandidates": candidate_count,
        "thresholdBitmapSha256": bitmap_sha256,
        "detectionCandidates": detection_candidates,
        "boxes": [box.tolist() for box in boxes],
        "crops": [
            {
                "index": index,
                "width": crop.shape[1],
                "height": crop.shape[0],
                "channels": crop.shape[2],
                "sha256Bgr8": sha256(crop.tobytes()),
                **(
                    {"pixelsBgr8Base64": base64.b64encode(crop.tobytes()).decode("ascii")}
                    if include_crop_pixels
                    else {}
                ),
            }
            for index, crop in enumerate(crops)
        ],
        "recognitionBatches": [],
        "decoded": [{} for _ in boxes],
        "lines": [],
    }
    for batch_index, (indices, rec_input) in enumerate(recognition_batches(crops, config["recognition"])):
        rec_output = np.asarray(rec_session.run(None, {rec_session.get_inputs()[0].name: rec_input})[0], dtype=np.float32)
        input_record = tensor_record(rec_input)
        output_record = tensor_record(rec_output)
        result["recognitionBatches"].append({"batchIndex": batch_index, "inputIndices": indices, "inputShape": input_record["shape"], "inputSha256Float32LE": input_record["sha256Float32LE"], "inputSamples": input_record["samples"], "outputShape": output_record["shape"], "outputSha256Float32LE": output_record["sha256Float32LE"], "outputSamples": output_record["samples"]})
        for source_index, decoded_value in zip(indices, decode(rec_output, characters)):
            result["decoded"][source_index] = decoded_value
    threshold = config["recognition"]["defaultScoreThreshold"]
    for box, decoded_value in zip(boxes, result["decoded"]):
        if decoded_value["text"] and decoded_value["confidence"] >= threshold:
            result["lines"].append({"text": decoded_value["text"], "confidence": decoded_value["confidence"], "box": box.tolist()})
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--pixels", type=Path, required=True)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--stride", type=int, required=True)
    parser.add_argument("--format", choices=["gray8", "rgb8", "bgr8", "rgba8"], required=True)
    arguments = parser.parse_args()
    print(json.dumps(run(arguments.bundle, arguments.pixels, arguments.width, arguments.height, arguments.stride, arguments.format), ensure_ascii=False, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
