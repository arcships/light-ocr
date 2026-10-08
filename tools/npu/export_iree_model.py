#!/usr/bin/env python3
"""Specialize a source-bound recognition graph for the IREE ONNX importer.

Only static Shape queries are folded; ONNX's version converter preserves the
opset migration. This is preparation for compilation, not numerical/device
qualification. The original model and npm packages are never overwritten.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

try:
    from .export_iree_hotspot import LOCK, WIDTHS, digest
except ImportError:
    from export_iree_hotspot import LOCK, WIDTHS, digest


def export(source: Path, output: Path, width: int) -> dict:
    import onnx
    from onnx import helper
    lock = json.loads(LOCK.read_text("utf-8"))
    if digest(source) != lock["sourceModelSha256"]:
        raise ValueError("source recognition model identity mismatch")
    if width not in WIDTHS:
        raise ValueError("width must belong to the locked recognition bucket contract")
    if output.exists():
        raise ValueError("output exists; select a fresh directory")
    model = onnx.load(source)
    if len(model.graph.input) != 1 or len(model.graph.output) != 1:
        raise ValueError("recognition graph must have one input and output")
    dims = model.graph.input[0].type.tensor_type.shape.dim
    if len(dims) != 4:
        raise ValueError("recognition input must be NCHW")
    for dimension, value in zip(dims, (1, 3, 48, width)):
        dimension.ClearField("dim_param")
        dimension.dim_value = value
    folded = []
    # Remove stale inferred intermediates before specializing the static input.
    # Each iteration may make another Shape input statically known.
    while True:
        model.graph.ClearField("value_info")
        model = onnx.shape_inference.infer_shapes(model, data_prop=True)
        shapes = {v.name: [d.dim_value for d in v.type.tensor_type.shape.dim]
                  for v in (*model.graph.input, *model.graph.value_info, *model.graph.output)}
        changed = False
        for node in model.graph.node:
            if node.op_type != "Shape":
                continue
            shape = shapes.get(node.input[0], [])
            if not shape or any(d <= 0 for d in shape):
                continue
            attrs = {a.name: helper.get_attribute_value(a) for a in node.attribute}
            selected = shape[attrs.get("start", 0):attrs.get("end", len(shape))]
            folded.append({"node": node.name, "sourceTensor": node.input[0], "value": selected})
            node.ClearField("input")
            node.ClearField("attribute")
            node.op_type = "Constant"
            tensor = helper.make_tensor("", onnx.TensorProto.INT64, [len(selected)], selected)
            node.attribute.append(helper.make_attribute("value", tensor))
            changed = True
        if not changed:
            break
    original_opsets = [{"domain": x.domain, "version": x.version} for x in model.opset_import]
    model = onnx.version_converter.convert_version(model, 17)
    model.graph.ClearField("value_info")
    model = onnx.shape_inference.infer_shapes(model, data_prop=True)
    output.mkdir(parents=True)
    artifact = output / "recognition-opset17.onnx"
    onnx.save(model, artifact)
    manifest = {"schemaVersion": "1.0", "stage": "development", "width": width,
                "sourceModelSha256": digest(source), "originalOpsets": original_opsets,
                "targetOpset": 17, "shapeQueriesFolded": folded,
                "inputShape": [1, 3, 48, width], "expectedOutputShape": [1, width // 8, 18710],
                "precision": "float32", "numericsValidated": False, "deviceValidated": False,
                "files": [{"path": artifact.name, "bytes": artifact.stat().st_size,
                           "sha256": digest(artifact)}]}
    (output / "model-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", "utf-8")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-model", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--width", type=int, choices=WIDTHS, default=384)
    args = parser.parse_args()
    result = export(args.source_model, args.output_dir, args.width)
    print(f"Specialized recognition width={result['width']}; folded {len(result['shapeQueriesFolded'])} Shape nodes")


if __name__ == "__main__":
    main()
