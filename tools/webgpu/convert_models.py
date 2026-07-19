#!/usr/bin/env python3
"""Derive deterministic WebGPU FP16 ONNX models from the locked FP32 bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

import onnx
from onnx import TensorProto
import numpy as np
import onnxruntime
from onnxruntime.transformers import float16


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUNDLE = ROOT / "models" / "generated" / "ppocrv6-small-onnx-20260714.2"
DEFAULT_OUTPUT = ROOT / "models" / "derived" / "webgpu-fp16-20260719.1"
CONVERSION_ID = "onnxruntime-float16-1.24.4-20260719.1"
ONNX_VERSION = "1.18.0"
ONNXRUNTIME_VERSION = "1.24.4"
SOURCE_MODELS = {
    "detection": {
        "path": "det/inference.onnx",
        "bytes": 9_880_512,
        "sha256": "d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e",
    },
    "recognition": {
        "path": "rec/inference.onnx",
        "bytes": 21_159_378,
        "sha256": "5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634",
    },
}
OUTPUT_MODELS = {
    "detection": {
        "path": "det/inference.onnx",
        "bytes": 4_973_761,
        "sha256": "2f0463ba51af55a9f49cae1f18cf905c578d4752236c30456325c817d8e47a1b",
    },
    "recognition": {
        "path": "rec/inference.onnx",
        "bytes": 10_624_002,
        "sha256": "f6ee9c86013fdd02cf9dfdf311813e1f2fe56e83d2b5f12994617dfa05d3b9cb",
    },
}


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def canonical_json(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def require_versions() -> None:
    if onnx.__version__ != ONNX_VERSION:
        raise RuntimeError(
            f"onnx version mismatch: expected {ONNX_VERSION}, got {onnx.__version__}"
        )
    if onnxruntime.__version__ != ONNXRUNTIME_VERSION:
        raise RuntimeError(
            "onnxruntime version mismatch: expected "
            f"{ONNXRUNTIME_VERSION}, got {onnxruntime.__version__}"
        )


def require_record(value: bytes, record: dict[str, object], label: str) -> None:
    if len(value) != record["bytes"] or sha256_bytes(value) != record["sha256"]:
        raise RuntimeError(f"{label} does not match its locked bytes and SHA-256")


def tensor_type_counts(model: onnx.ModelProto) -> dict[str, int]:
    counts: dict[str, int] = {}
    for initializer in model.graph.initializer:
        name = TensorProto.DataType.Name(initializer.data_type).lower()
        counts[name] = counts.get(name, 0) + 1
    return dict(sorted(counts.items()))


def verify_runtime_model(encoded: bytes, name: str) -> None:
    options = onnxruntime.SessionOptions()
    options.graph_optimization_level = onnxruntime.GraphOptimizationLevel.ORT_ENABLE_EXTENDED
    session = onnxruntime.InferenceSession(
        encoded, sess_options=options, providers=["CPUExecutionProvider"]
    )
    shape = (1, 3, 64, 64) if name == "detection" else (1, 3, 48, 320)
    count = int(np.prod(shape))
    values = np.linspace(-1.0, 1.0, count, dtype=np.float16).reshape(shape)
    first = session.run(None, {session.get_inputs()[0].name: values})
    second = session.run(None, {session.get_inputs()[0].name: values})
    if (
        len(first) != 1
        or len(second) != 1
        or first[0].dtype != np.float16
        or first[0].shape != second[0].shape
        or not np.isfinite(first[0]).all()
        or not np.array_equal(first[0], second[0])
    ):
        raise RuntimeError(f"converted {name} model failed deterministic FP16 runtime verification")


def convert_model(
    source: Path, record: dict[str, object], name: str
) -> tuple[bytes, dict[str, object]]:
    model = onnx.load(source, load_external_data=False)
    converted = float16.convert_float_to_float16(
        model,
        min_positive_val=5.96e-08,
        max_finite_val=65_504.0,
        keep_io_types=False,
        disable_shape_infer=False,
        op_block_list=None,
        node_block_list=None,
        force_fp16_initializers=False,
    )
    onnx.checker.check_model(converted, full_check=True)
    graph_values = [*converted.graph.input, *converted.graph.output]
    if not graph_values or any(
        value.type.tensor_type.elem_type != TensorProto.FLOAT16
        for value in graph_values
    ):
        raise RuntimeError("converted model graph inputs and outputs must all be float16")
    encoded = converted.SerializeToString(deterministic=True)
    require_record(encoded, record, str(source))
    verify_runtime_model(encoded, name)
    metadata = {
        "graphInputs": [value.name for value in converted.graph.input],
        "graphOutputs": [value.name for value in converted.graph.output],
        "graphIoType": "float16",
        "nodes": len(converted.graph.node),
        "initializerTypes": tensor_type_counts(converted),
    }
    return encoded, metadata


def generate(bundle: Path, output: Path) -> dict[str, object]:
    require_versions()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        models: dict[str, object] = {}
        for name in ("detection", "recognition"):
            source_record = SOURCE_MODELS[name]
            output_record = OUTPUT_MODELS[name]
            source = bundle / str(source_record["path"])
            if not source.is_file():
                raise RuntimeError(f"source model is missing: {source}")
            source_bytes = source.read_bytes()
            require_record(source_bytes, source_record, str(source))
            converted, metadata = convert_model(source, output_record, name)
            destination = temporary / str(output_record["path"])
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(converted)
            models[name] = {
                "source": source_record,
                "output": output_record,
                **metadata,
            }
        provenance = {
            "schemaVersion": "1.0",
            "artifactId": output.name,
            "conversionId": CONVERSION_ID,
            "converter": {
                "name": "onnxruntime.transformers.float16.convert_float_to_float16",
                "onnxVersion": ONNX_VERSION,
                "onnxruntimeVersion": ONNXRUNTIME_VERSION,
                "keepIoTypes": False,
                "disableShapeInfer": False,
                "forceFp16Initializers": False,
                "minPositiveValue": 5.96e-08,
                "maxFiniteValue": 65_504.0,
                "opBlockList": list(float16.DEFAULT_OP_BLOCK_LIST),
            },
            "runtimeContract": {
                "precision": "fp16",
                "graphOptimizationLevel": "extended",
                "cpuPartition": "allow-required",
                "requiredCpuOperators": ["Concat", "Gather", "Slice"],
            },
            "models": models,
        }
        (temporary / "provenance.json").write_bytes(canonical_json(provenance))
        if output.exists():
            shutil.rmtree(output)
        os.replace(temporary, output)
        return provenance
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, default=DEFAULT_BUNDLE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args()
    provenance = generate(arguments.bundle.resolve(), arguments.output.resolve())
    print(canonical_json(provenance).decode("utf-8"), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
