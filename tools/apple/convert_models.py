#!/usr/bin/env python3
"""Derive the locked FP16 Core ML programs from the PP-OCRv6 ONNX bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import uuid

import coremltools as ct
from coremltools.models.utils import MultiFunctionDescriptor, save_multifunction
from coremltools.proto import Model_pb2
import numpy as np
import onnx
from onnx import helper
from onnx2torch import convert
import torch


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUNDLE = ROOT / "models" / "generated" / "ppocrv6-small-onnx-20260714.2"
DEFAULT_OUTPUT = ROOT / "models" / "generated" / "apple-fp16-20260715.1"
DETECTION_SOURCE_SHA256 = "d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e"
RECOGNITION_SOURCE_SHA256 = "5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634"
DETECTION_PACKAGE_SHA256 = "2097bd785947c6bc239bfcb27599362c48ec78bab72f439583e41a585b727f76"
RECOGNITION_PACKAGE_SHA256 = "c54a0719cbde2d93e65eb40dd01fff5b78373b5aaaaa648fee614d3ef3615f4b"
WIDTHS = tuple(range(320, 3201, 32))
PACKAGE_MANIFEST_NAMESPACE = uuid.UUID("c6d6765d-4af4-50eb-9717-48bb41451b26")
COREMLTOOLS_CONVERSION_DATE_KEY = "com.github.apple.coremltools.conversion_date"


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def directory_sha256(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(relative)
        digest.update(b"\0")
        digest.update(file_sha256(path).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def require_source(path: Path, expected_sha256: str) -> None:
    if not path.is_file():
        raise RuntimeError(f"source model is missing: {path}")
    actual = file_sha256(path)
    if actual != expected_sha256:
        raise RuntimeError(
            f"source model hash mismatch: expected {expected_sha256}, got {actual}: {path}"
        )


def canonicalize_package_manifest(package: Path) -> None:
    """Replace Core ML's random package entry UUIDs with stable UUIDv5 values."""
    path = package / "Manifest.json"
    manifest = json.loads(path.read_text("utf-8"))
    entries = manifest.get("itemInfoEntries")
    root_identifier = manifest.get("rootModelIdentifier")
    if not isinstance(entries, dict) or root_identifier not in entries:
        raise RuntimeError(f"Core ML package manifest is invalid: {path}")
    canonical_entries: dict[str, object] = {}
    canonical_root = ""
    for identifier, entry in sorted(
        entries.items(), key=lambda item: (item[1].get("path", ""), item[0])
    ):
        entry_path = entry.get("path")
        if not isinstance(entry_path, str) or not entry_path:
            raise RuntimeError(f"Core ML package manifest entry has no path: {path}")
        canonical_identifier = str(
            uuid.uuid5(PACKAGE_MANIFEST_NAMESPACE, entry_path)
        ).upper()
        if canonical_identifier in canonical_entries:
            raise RuntimeError(f"Core ML package manifest path is duplicated: {entry_path}")
        canonical_entries[canonical_identifier] = entry
        if identifier == root_identifier:
            canonical_root = canonical_identifier
    if not canonical_root:
        raise RuntimeError(f"Core ML package root identifier is invalid: {path}")
    manifest["itemInfoEntries"] = canonical_entries
    manifest["rootModelIdentifier"] = canonical_root
    path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=4, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def canonicalize_package_models(package: Path) -> None:
    """Remove volatile metadata and deterministically serialize model protobufs."""
    model_paths = sorted(package.rglob("*.mlmodel"))
    if not model_paths:
        raise RuntimeError(f"Core ML package contains no model protobuf: {package}")
    for path in model_paths:
        model = Model_pb2.Model()
        model.ParseFromString(path.read_bytes())
        metadata = model.description.metadata.userDefined
        if COREMLTOOLS_CONVERSION_DATE_KEY in metadata:
            del metadata[COREMLTOOLS_CONVERSION_DATE_KEY]
        path.write_bytes(model.SerializeToString(deterministic=True))


def canonicalize_package(package: Path) -> None:
    canonicalize_package_models(package)
    canonicalize_package_manifest(package)


def normalized_onnx(path: Path) -> onnx.ModelProto:
    model = onnx.load(path)
    for node in model.graph.node:
        attributes = {
            attribute.name: helper.get_attribute_value(attribute)
            for attribute in node.attribute
        }
        if attributes.get("auto_pad") != b"SAME_UPPER":
            continue
        kernel = attributes.get("kernel_shape")
        strides = attributes.get("strides")
        dilations = attributes.get("dilations", [1, 1])
        if kernel != [2, 2] or strides != [1, 1] or dilations != [1, 1]:
            raise RuntimeError(
                f"unsupported SAME_UPPER normalization at {node.name}: "
                f"kernel={kernel}, strides={strides}, dilations={dilations}"
            )
        del node.attribute[:]
        for key, value in attributes.items():
            if key != "auto_pad":
                node.attribute.append(helper.make_attribute(key, value))
        node.attribute.append(helper.make_attribute("pads", [0, 0, 1, 1]))
    onnx.checker.check_model(model)
    return model


def traced_model(model: onnx.ModelProto, shape: tuple[int, ...]) -> torch.jit.ScriptModule:
    module = convert(model).eval()
    with torch.inference_mode():
        return torch.jit.trace(module, torch.zeros(shape), strict=False).eval()


def convert_program(
    model: onnx.ModelProto,
    shape: tuple[object, ...],
    trace_shape: tuple[int, ...],
    destination: Path,
) -> None:
    shutil.rmtree(destination, ignore_errors=True)
    traced = traced_model(model, trace_shape)
    program = ct.convert(
        traced,
        convert_to="mlprogram",
        minimum_deployment_target=ct.target.macOS15,
        compute_precision=ct.precision.FLOAT16,
        inputs=[ct.TensorType(name="x", shape=shape, dtype=np.float32)],
    )
    program.author = "Arcships"
    program.license = "Apache-2.0"
    program.user_defined_metadata["com.arcships.light-ocr.precision"] = "fp16"
    program.save(str(destination))
    canonicalize_package(destination)


def generate(bundle: Path, output: Path, keep_intermediates: bool) -> dict[str, object]:
    detection_source = bundle / "det" / "inference.onnx"
    recognition_source = bundle / "rec" / "inference.onnx"
    require_source(detection_source, DETECTION_SOURCE_SHA256)
    require_source(recognition_source, RECOGNITION_SOURCE_SHA256)
    detection_onnx = normalized_onnx(detection_source)
    recognition_onnx = normalized_onnx(recognition_source)

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_parent = output.parent
    temporary = Path(
        tempfile.mkdtemp(prefix=output.name + ".", suffix=".tmp", dir=temporary_parent)
    )
    try:
        detection_package = temporary / "detector-fp16.mlpackage"
        detection_shape = (
            1,
            3,
            ct.RangeDim(32, 960, default=768),
            ct.RangeDim(32, 960, default=960),
        )
        convert_program(
            detection_onnx,
            detection_shape,
            (1, 3, 768, 960),
            detection_package,
        )

        functions = temporary / "recognizer-functions"
        functions.mkdir()
        descriptor = MultiFunctionDescriptor()
        for width in WIDTHS:
            static_package = functions / f"w{width:04d}.mlpackage"
            shape = (1, 3, 48, width)
            convert_program(recognition_onnx, shape, shape, static_package)
            descriptor.add_function(str(static_package), "main", f"w{width:04d}")
        descriptor.default_function_name = "w0320"
        recognition_package = temporary / "recognizer-fp16.mlpackage"
        save_multifunction(descriptor, str(recognition_package))
        canonicalize_package(recognition_package)
        if not keep_intermediates:
            shutil.rmtree(functions)

        detection_package_sha256 = directory_sha256(detection_package)
        recognition_package_sha256 = directory_sha256(recognition_package)
        if detection_package_sha256 != DETECTION_PACKAGE_SHA256:
            raise RuntimeError(
                "derived detector hash changed: " + detection_package_sha256
            )
        if recognition_package_sha256 != RECOGNITION_PACKAGE_SHA256:
            raise RuntimeError(
                "derived recognizer hash changed: " + recognition_package_sha256
            )
        provenance = {
            "schemaVersion": "1.0",
            "artifactId": output.name,
            "conversion": {
                "coremltools": ct.__version__,
                "onnx": onnx.__version__,
                "onnx2torch": "1.5.15",
                "torch": torch.__version__,
                "minimumMacOS": "15.0",
                "precision": "fp16",
            },
            "source": {
                "bundleId": bundle.name,
                "detectionSha256": DETECTION_SOURCE_SHA256,
                "recognitionSha256": RECOGNITION_SOURCE_SHA256,
            },
            "detection": {
                "modelId": "PP-OCRv6_small_det_coreml_fp16_range_v1",
                "package": detection_package.name,
                "packageSha256": detection_package_sha256,
                "inputName": "x",
                "outputName": "var_1524",
                "shapePolicy": "nchw-bounded-range-32-960-v1",
            },
            "recognition": {
                "modelId": "PP-OCRv6_small_rec_coreml_fp16_w32_v1",
                "package": recognition_package.name,
                "packageSha256": recognition_package_sha256,
                "inputName": "x",
                "outputName": "var_2113",
                "shapePolicy": "nchw-static-width-multiple-32-v1",
                "widths": list(WIDTHS),
                "functionFormat": "w%04u",
            },
        }
        (temporary / "provenance.json").write_text(
            json.dumps(provenance, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        shutil.rmtree(output, ignore_errors=True)
        os.replace(temporary, output)
        return provenance
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, default=DEFAULT_BUNDLE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--keep-intermediates", action="store_true")
    arguments = parser.parse_args()
    provenance = generate(
        arguments.bundle.resolve(),
        arguments.output.resolve(),
        arguments.keep_intermediates,
    )
    print(json.dumps(provenance, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
