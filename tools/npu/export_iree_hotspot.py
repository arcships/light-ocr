#!/usr/bin/env python3
"""Export a real Small recognition MLP block for ahead-of-time compilation.

This development tool does not execute inference or alter release packages.
FP32 ONNX is preserved as the reference graph; MLIR uses BF16 matmul operands,
FP32 accumulation, bias, GELU and residual. Numerical parity is not implied.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

LOCK = Path(__file__).with_name("iree-source.lock.json")
WIDTHS = (320, 384, 480, 544, 576, 608, 704, 736, 832, 960, 1056, 1184,
          1248, 1376, 1600, 1984, 2240, 2560, 2880, 3200)


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def tensor_type(shape, dtype="f32") -> str:
    return f"tensor<{'x'.join(str(x) for x in shape)}x{dtype}>"


def bf16_bits(values):
    import numpy as np
    words = np.asarray(values, dtype="<f4").view("<u4")
    # Round to nearest, ties to even. Model weights must be finite.
    if not np.isfinite(values).all():
        raise ValueError("non-finite source weights are unsupported")
    return ((words + 0x7FFF + ((words >> 16) & 1)) >> 16).astype("<u2")


def dense(values, dtype="f32") -> str:
    import numpy as np
    bits = bf16_bits(values) if dtype == "bf16" else np.asarray(values, dtype="<f4")
    return f'dense<"0x{bits.tobytes().hex().upper()}"> : {tensor_type(values.shape, dtype)}'


def mlir_block(shape, weights, biases, full: bool) -> str:
    """Lower 1x1 Conv -> bias -> exact GELU -> 1x1 Conv -> bias -> residual."""
    n, k, h, w = shape
    m, hidden = n * h * w, weights[0].shape[1]
    nchw, nhwc = tensor_type(shape), tensor_type((n, h, w, k))
    x32, x16 = tensor_type((m, k)), tensor_type((m, k), "bf16")
    h32, h16 = tensor_type((m, hidden)), tensor_type((m, hidden), "bf16")
    b1, b2 = tensor_type((hidden,)), tensor_type((k,))
    lines = [
        "// Source-bound development IR; not a qualified model artifact.",
        "#id = affine_map<(d0, d1) -> (d0, d1)>",
        "#bias = affine_map<(d0, d1) -> (d1)>",
        f"module {{\n  func.func @{'mlp_residual' if full else 'projection'}(%input: {nchw}) -> {nchw if full else h32} {{",
        f"    %w1 = arith.constant {dense(weights[0], 'bf16')}",
        f"    %b1 = arith.constant {dense(biases[0])}",
        "    %zero = arith.constant 0.0 : f32",
        f"    %layout_empty = tensor.empty() : {nhwc}",
        f"    %nhwc = linalg.transpose ins(%input : {nchw}) outs(%layout_empty : {nhwc}) permutation = [0, 2, 3, 1]",
        f"    %flat = tensor.collapse_shape %nhwc [[0, 1, 2], [3]] : {nhwc} into {x32}",
    ]

    def cast(name, source, src_type, dst_type):
        lines.extend([
            f"    %{name}_empty = tensor.empty() : {dst_type}",
            f'    %{name} = linalg.generic {{indexing_maps = [#id, #id], iterator_types = ["parallel", "parallel"]}} ins(%{source} : {src_type}) outs(%{name}_empty : {dst_type}) {{',
            "      ^bb0(%a: f32, %unused: bf16):",
            "        %v = arith.truncf %a : f32 to bf16",
            "        linalg.yield %v : bf16",
            f"    }} -> {dst_type}",
        ])

    def matmul(name, source, weight, src_type, weight_type, result_type):
        lines.extend([
            f"    %{name}_empty = tensor.empty() : {result_type}",
            f"    %{name}_zero = linalg.fill ins(%zero : f32) outs(%{name}_empty : {result_type}) -> {result_type}",
            f"    %{name} = linalg.matmul ins(%{source}, %{weight} : {src_type}, {weight_type}) outs(%{name}_zero : {result_type}) -> {result_type}",
        ])

    def bias(name, source, bias_name, source_type, bias_type):
        lines.extend([
            f"    %{name}_empty = tensor.empty() : {source_type}",
            f'    %{name} = linalg.generic {{indexing_maps = [#id, #bias, #id], iterator_types = ["parallel", "parallel"]}} ins(%{source}, %{bias_name} : {source_type}, {bias_type}) outs(%{name}_empty : {source_type}) {{',
            "      ^bb0(%a: f32, %b: f32, %unused: f32):",
            "        %v = arith.addf %a, %b : f32",
            "        linalg.yield %v : f32",
            f"    }} -> {source_type}",
        ])

    cast("x_bf16", "flat", x32, x16)
    matmul("projected", "x_bf16", "w1", x16, tensor_type(weights[0].shape, "bf16"), h32)
    bias("biased", "projected", "b1", h32, b1)
    if not full:
        lines.extend([f"    return %biased : {h32}", "  }", "}"])
        return "\n".join(lines) + "\n"
    lines.extend([
        f"    %w2 = arith.constant {dense(weights[1], 'bf16')}",
        f"    %b2 = arith.constant {dense(biases[1])}",
        "    %sqrt2 = arith.constant 1.4142135623730951 : f32",
        "    %half = arith.constant 0.5 : f32",
        "    %one = arith.constant 1.0 : f32",
        f"    %gelu_empty = tensor.empty() : {h32}",
        f'    %gelu = linalg.generic {{indexing_maps = [#id, #id], iterator_types = ["parallel", "parallel"]}} ins(%biased : {h32}) outs(%gelu_empty : {h32}) {{',
        "      ^bb0(%a: f32, %unused: f32):",
        "        %scaled = arith.divf %a, %sqrt2 : f32",
        "        %erf = math.erf %scaled : f32",
        "        %plus = arith.addf %erf, %one : f32",
        "        %product = arith.mulf %a, %plus : f32",
        "        %v = arith.mulf %product, %half : f32",
        "        linalg.yield %v : f32",
        f"    }} -> {h32}",
    ])
    cast("hidden_bf16", "gelu", h32, h16)
    matmul("contracted", "hidden_bf16", "w2", h16, tensor_type(weights[1].shape, "bf16"), x32)
    bias("final_biased", "contracted", "b2", x32, b2)
    lines.extend([
        f"    %res_empty = tensor.empty() : {x32}",
        f'    %res = linalg.generic {{indexing_maps = [#id, #id, #id], iterator_types = ["parallel", "parallel"]}} ins(%final_biased, %flat : {x32}, {x32}) outs(%res_empty : {x32}) {{',
        "      ^bb0(%a: f32, %b: f32, %unused: f32):",
        "        %v = arith.addf %a, %b : f32",
        "        linalg.yield %v : f32",
        f"    }} -> {x32}",
        f"    %expanded = tensor.expand_shape %res [[0, 1, 2], [3]] output_shape [{n}, {h}, {w}, {k}] : {x32} into {nhwc}",
        f"    %out_empty = tensor.empty() : {nchw}",
        f"    %out = linalg.transpose ins(%expanded : {nhwc}) outs(%out_empty : {nchw}) permutation = [0, 3, 1, 2]",
        f"    return %out : {nchw}", "  }", "}",
    ])
    return "\n".join(lines) + "\n"


def mlir_projection_matmul(shape, weight, padded=False) -> str:
    """Isolate the real first Conv's matrix core with an explicit BF16 ABI.

    Layout conversion, input rounding and bias remain outside this diagnostic
    kernel. This is not a replacement for either complete exported block.
    """
    n, k, h, w = shape
    rows = ((n * h * w + 127) // 128) * 128 if padded else n * h * w
    lhs = tensor_type((rows, k), "bf16")
    rhs = tensor_type(weight.shape, "bf16")
    result = tensor_type((rows, weight.shape[1]))
    return "\n".join([
        "// Diagnostic matrix core only; external layout/cast/bias required.",
        "module {",
        f"  func.func @projection_matmul(%input: {lhs}) -> {result} {{",
        f"    %weight = arith.constant {dense(weight, 'bf16')}",
        "    %zero = arith.constant 0.0 : f32",
        f"    %empty = tensor.empty() : {result}",
        f"    %initial = linalg.fill ins(%zero : f32) outs(%empty : {result}) -> {result}",
        f"    %out = linalg.matmul ins(%input, %weight : {lhs}, {rhs}) outs(%initial : {result}) -> {result}",
        f"    return %out : {result}", "  }", "}", "",
    ])


def export(source: Path, output: Path, width: int) -> dict:
    import numpy as np
    import onnx
    from onnx import numpy_helper
    lock = json.loads(LOCK.read_text("utf-8"))
    if digest(source) != lock["sourceModelSha256"]:
        raise ValueError("recognition model does not match the locked Small 0.3.4 source")
    if width not in WIDTHS:
        raise ValueError("width must belong to the locked recognition bucket contract")
    if output.exists():
        raise ValueError("output exists; select a fresh directory")
    model = onnx.load(source)
    for d, value in zip(model.graph.input[0].type.tensor_type.shape.dim, (1, 3, 48, width)):
        d.ClearField("dim_param")
        d.dim_value = value
    model = onnx.shape_inference.infer_shapes(model)
    by_name = {n.name: n for n in model.graph.node}
    first, second, residual = (by_name[k] for k in ("Conv.49", "Conv.50", "Add.144"))
    start, end = first.input[0], residual.output[0]
    shapes = {v.name: [d.dim_value for d in v.type.tensor_type.shape.dim]
              for v in (*model.graph.input, *model.graph.value_info, *model.graph.output)}
    shape = shapes[start]
    if shape != [1, 384, 3, width // 4] or shapes[end] != shape:
        raise ValueError("hotspot shape differs from the source-bound contract")
    constants = {t.name: numpy_helper.to_array(t) for t in model.graph.initializer}
    producers = {v: n for n in model.graph.node for v in n.output}

    def constant(name):
        if name in constants:
            return constants[name]
        node = producers.get(name)
        if node is None:
            raise ValueError(f"unresolved constant: {name}")
        attrs = {a.name: onnx.helper.get_attribute_value(a) for a in node.attribute}
        if node.op_type == "Constant":
            value = numpy_helper.to_array(attrs["value"])
        elif node.op_type == "Identity":
            value = constant(node.input[0])
        elif node.op_type == "Reshape":
            value = constant(node.input[0]).reshape(tuple(int(x) for x in constant(node.input[1])))
        else:
            raise ValueError(f"unsupported constant producer: {node.op_type}")
        constants[name] = value
        return value

    weights, biases = [], []
    for conv, bias_node in ((first, by_name["Add.138"]), (second, by_name["Add.142"])):
        attrs = {a.name: onnx.helper.get_attribute_value(a) for a in conv.attribute}
        weight = constant(conv.input[1])
        if (weight.shape[2:] != (1, 1) or attrs.get("group", 1) != 1 or
                list(attrs.get("strides", [1, 1])) != [1, 1] or
                any(attrs.get("pads", [0, 0, 0, 0]))):
            raise ValueError("hotspot must be dense stride-one 1x1 convolution")
        weights.append(weight[:, :, 0, 0].T.copy())
        bias_input = next(v for v in bias_node.input if v != conv.output[0])
        value = constant(bias_input)
        if value.shape != (1, weight.shape[0], 1, 1):
            raise ValueError("unexpected convolution bias broadcast shape")
        biases.append(value.reshape(-1).copy())
    output.mkdir(parents=True)
    reference = onnx.utils.Extractor(model).extract_model([start], [end])
    onnx.save(reference, output / "reference-fp32.onnx")
    for name, full in (("projection", False), ("mlp-residual", True)):
        (output / f"{name}.mlir").write_text(mlir_block(shape, weights, biases, full), "utf-8")
    (output / "projection-matmul.mlir").write_text(mlir_projection_matmul(shape, weights[0]), "utf-8")
    (output / "projection-matmul-padded.mlir").write_text(mlir_projection_matmul(shape, weights[0], True), "utf-8")
    np.savez(output / "weights-fp32.npz", w1=weights[0], b1=biases[0], w2=weights[1], b2=biases[1])
    files = [{"path": p.name, "bytes": p.stat().st_size, "sha256": digest(p)}
             for p in sorted(output.iterdir())]
    manifest = {"schemaVersion": "1.0", "stage": "development", "deviceValidated": False,
                "numericsValidated": False, "sourceModelSha256": digest(source),
                "width": width, "input": {"name": start, "shape": shape, "type": "float32"},
                "output": {"name": end, "shape": shape, "type": "float32"},
                "variants": {
                    "projection-matmul-padded": {
                        "inputShape": [((shape[0] * shape[2] * shape[3] + 127) // 128) * 128, shape[1]],
                        "outputShape": [((shape[0] * shape[2] * shape[3] + 127) // 128) * 128, weights[0].shape[1]],
                        "inputType": "bfloat16", "outputType": "float32",
                        "validRows": shape[0] * shape[2] * shape[3],
                        "callerRequirements": ["zero-fill-extra-input-rows", "crop-extra-output-rows"],
                        "excludedOperations": ["layout-conversion", "input-rounding", "bias"]},
                    "projection-matmul": {"inputShape": [shape[0] * shape[2] * shape[3], shape[1]],
                                          "outputShape": [shape[0] * shape[2] * shape[3], weights[0].shape[1]],
                                          "inputType": "bfloat16", "outputType": "float32",
                                          "excludedOperations": ["layout-conversion", "input-rounding", "bias"]},
                    "projection": {"inputShape": shape,
                                   "outputShape": [shape[0] * shape[2] * shape[3], weights[0].shape[1]],
                                   "interfaceType": "float32"},
                    "mlp-residual": {"inputShape": shape, "outputShape": shape,
                                     "interfaceType": "float32"}},
                "sourceNodes": [n.name for n in reference.graph.node],
                "precision": {"operands": "bf16", "accumulation": "float32", "gelu": "erf-float32"},
                "transformations": ["NCHW-to-NHWC", "1x1-conv-to-matmul", "bf16-round-nearest-even"],
                "files": files}
    (output / "hotspot-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", "utf-8")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-model", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--width", type=int, default=384, choices=WIDTHS)
    args = parser.parse_args()
    result = export(args.source_model, args.output_dir, args.width)
    print(f"Exported source-bound hotspot: width={result['width']}, {len(result['sourceNodes'])} nodes")


if __name__ == "__main__":
    main()
