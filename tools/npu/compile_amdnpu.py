#!/usr/bin/env python3
"""Compile locked FP32 recognition into embedded Ryzen AI BF16 EP contexts.

Run using the vendor SDK's Python interpreter. Neither Python nor a compiler
is required by the shipped light-ocr process.
"""
from __future__ import annotations

import argparse
import json
import os
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


def compile_models(source: Path, expected_sha256: str, configuration: Path, output: Path,
                   widths: list[int] | None = None) -> dict:
    import onnx
    import onnxruntime as ort
    if digest(source) != expected_sha256:
        raise ValueError("source recognition model does not match its immutable bundle hash")
    if "VitisAIExecutionProvider" not in ort.get_available_providers():
        raise ValueError("run this tool with the Ryzen AI SDK Python interpreter")
    selected_widths = recognition_widths() if widths is None else sorted(set(widths))
    if not selected_widths or any(width not in recognition_widths() for width in selected_widths):
        raise ValueError("recognition widths must belong to the locked 20-bucket contract")
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
    for width in selected_widths:
        shaped = onnx.ModelProto()
        shaped.CopyFrom(model)
        for dimension, value in zip(shaped.graph.input[0].type.tensor_type.shape.dim, [1, 3, 48, width]):
            dimension.ClearField("dim_param")
            dimension.dim_value = value
        static_path = output / f"rec-{width}-source.onnx"
        context_path = output / f"rec-{width}-context.onnx"
        onnx.save_model(shaped, static_path)
        options = ort.SessionOptions()
        options.intra_op_num_threads = 1
        options.inter_op_num_threads = 1
        options.add_session_config_entry("ep.context_enable", "1")
        options.add_session_config_entry("ep.context_file_path", str(context_path.absolute()))
        options.add_session_config_entry("ep.context_embed_mode", "1")
        session = ort.InferenceSession(str(static_path), sess_options=options,
            providers=["VitisAIExecutionProvider", "CPUExecutionProvider"],
            provider_options=[{"config_file": str(config_path.absolute()),
                               "cache_dir": str((output / "compiler-cache").absolute()),
                               "cache_key": f"rec-{width}",
                               "enable_cache_file_io_in_mem": "0"}, {}])
        inputs, outputs = session.get_inputs(), session.get_outputs()
        if (len(inputs) != 1 or inputs[0].type != "tensor(float)" or
                inputs[0].shape != [1, 3, 48, width] or len(outputs) != 1 or
                outputs[0].type != "tensor(float)" or len(outputs[0].shape) != 3):
            raise ValueError("AMD compiler produced an incompatible recognition tensor")
        del session
        count = validate_context(context_path)
        # Compilation and structural validation do not execute the model.
        # Hardware inference is a separate, optional activity.
        models.append({"width": width, "artifact": record(context_path, output), "contextCount": count})
        static_path.unlink()
        print(f"Compiled recognition bucket {width} (inference not executed)", flush=True)
    manifest = {"schemaVersion": "1.0", "sourceModelSha256": expected_sha256,
                "precision": "bf16", "runtimeVersion": ort.__version__,
                "inferenceValidated": False,
                "compilerConfiguration": record(config_path, output), "recognitionModels": models}
    (output / "compile-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", "utf-8")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-model", type=Path, required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--configuration", type=Path, default=Path(__file__).with_name("vaip_config.json"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--width", type=int, action="append",
                        help="Compile selected buckets; SDK import still requires all 20")
    args = parser.parse_args()
    # Vendor compiler components write signatures relative to the current
    # directory. Keep those files out of the repository and isolate buckets
    # when independent compiler processes run concurrently.
    args.source_model = args.source_model.resolve()
    args.configuration = args.configuration.resolve()
    args.output_dir = args.output_dir.resolve()
    working = args.output_dir.with_name(args.output_dir.name + ".work")
    working.mkdir(parents=True, exist_ok=True)
    os.chdir(working)
    compile_models(args.source_model, args.source_sha256, args.configuration, args.output_dir, args.width)


if __name__ == "__main__":
    main()
