#!/usr/bin/env python3
"""Compile locked FP32 recognition into embedded Ryzen AI BF16 EP contexts.

Run using the vendor SDK's Python interpreter. Neither Python nor a compiler
is required by the shipped light-ocr process.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil

try:
    from .sdk import digest, record, recognition_widths
except ImportError:
    from sdk import digest, record, recognition_widths


def validate_context(path: Path) -> int:
    import onnx
    model = onnx.load_model(path, load_external_data=False)
    if any(item.data_location == onnx.TensorProto.EXTERNAL for item in model.graph.initializer):
        raise ValueError("AMD deployment models must embed all external weights")
    contexts = [node for node in model.graph.node if node.op_type == "EPContext"]
    if not contexts:
        raise ValueError("VitisAI compiled no NPU context; refusing CPU-only derived models")
    for node in contexts:
        attributes = {item.name: onnx.helper.get_attribute_value(item) for item in node.attribute}
        if (attributes.get("embed_mode") != 1 or not attributes.get("ep_cache_context") or
                attributes.get("source") not in {b"VitisAI", b"VitisAIExecutionProvider"}):
            raise ValueError("AMD EP contexts must be embedded and owned by VitisAI")
    return len(contexts)


def compile_models(source: Path, expected_sha256: str, configuration: Path, output: Path) -> dict:
    import numpy as np
    import onnx
    import onnxruntime as ort
    if digest(source) != expected_sha256:
        raise ValueError("source recognition model does not match its immutable bundle hash")
    if "VitisAIExecutionProvider" not in ort.get_available_providers():
        raise ValueError("run this tool with the Ryzen AI SDK Python interpreter")
    config = json.loads(configuration.read_text("utf-8"))
    if (config.get("target") != "VAIML" or
            not any(item.get("name") == "vaiml_partition" for item in config.get("passes", []))):
        raise ValueError("recognition requires a VAIML BF16 compiler configuration")
    if output.exists():
        raise ValueError("output already exists; derived model identities are immutable")
    output.mkdir(parents=True)
    config_path = output / "vaip_config.json"
    shutil.copyfile(configuration, config_path)
    model = onnx.load_model(source, load_external_data=False)
    if len(model.graph.input) != 1 or len(model.graph.output) != 1:
        raise ValueError("recognition model must expose one input and output")
    source_shape = model.graph.input[0].type.tensor_type.shape.dim
    if len(source_shape) != 4:
        raise ValueError("recognition input must be NCHW")
    models = []
    for width in recognition_widths():
        shaped = onnx.ModelProto()
        shaped.CopyFrom(model)
        for dimension, value in zip(shaped.graph.input[0].type.tensor_type.shape.dim, [1, 3, 48, width]):
            dimension.ClearField("dim_param")
            dimension.dim_value = value
        static_path = output / f"rec-{width}-source.onnx"
        context_path = output / f"rec-{width}-context.onnx"
        onnx.save_model(shaped, static_path)
        options = ort.SessionOptions()
        options.add_session_config_entry("ep.context_enable", "1")
        options.add_session_config_entry("ep.context_file_path", str(context_path.absolute()))
        options.add_session_config_entry("ep.context_embed_mode", "1")
        session = ort.InferenceSession(str(static_path), sess_options=options,
            providers=["VitisAIExecutionProvider", "CPUExecutionProvider"],
            provider_options=[{"config_file": str(config_path.absolute())}, {}])
        result = session.run(None, {session.get_inputs()[0].name: np.zeros([1, 3, 48, width], dtype=np.float32)})
        if len(result) != 1 or result[0].dtype != np.float32 or result[0].ndim != 3:
            raise ValueError("AMD compiler produced an incompatible recognition tensor")
        del session
        count = validate_context(context_path)
        # Reopen the exact deployment artifact: compilation success alone is
        # insufficient evidence that the embedded context can be loaded.
        deployed = ort.InferenceSession(str(context_path), providers=["VitisAIExecutionProvider", "CPUExecutionProvider"],
            provider_options=[{"config_file": str(config_path.absolute())}, {}])
        deployed.run(None, {deployed.get_inputs()[0].name: np.zeros([1, 3, 48, width], dtype=np.float32)})
        del deployed
        models.append({"width": width, "artifact": record(context_path, output), "contextCount": count})
        static_path.unlink()
        print(f"Compiled and reopened recognition bucket {width}", flush=True)
    manifest = {"schemaVersion": "1.0", "sourceModelSha256": expected_sha256,
                "precision": "bf16", "runtimeVersion": ort.__version__,
                "compilerConfiguration": record(config_path, output), "recognitionModels": models}
    (output / "compile-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", "utf-8")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-model", type=Path, required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--configuration", type=Path, default=Path(__file__).with_name("vaip_config.json"))
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    compile_models(args.source_model, args.source_sha256, args.configuration, args.output_dir)


if __name__ == "__main__":
    main()
