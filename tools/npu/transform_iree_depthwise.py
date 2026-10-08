#!/usr/bin/env python3
"""Convert static depthwise Linalg convolutions from NCHW/CHW to NHWC/HWC.

Run inside the IREE frontend environment. Ordinary convolutions are handled
by IREE's channels-last preprocessing pass. Scalar computation is preserved.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def transform(source: Path, output: Path) -> int:
    from iree.compiler import ir
    if output.exists():
        raise ValueError("output exists; select a fresh IR path")
    with ir.Context(), ir.Location.unknown():
        module = ir.Module.parse(source.read_text("utf-8"))
        candidates = []

        def walk(operation):
            if operation.name == "linalg.depthwise_conv_2d_nchw_chw":
                candidates.append(operation)
            for region in operation.regions:
                for block in region.blocks:
                    for child in block.operations:
                        walk(child.operation)

        walk(module.operation)

        def transpose(value, permutation):
            source_type = ir.RankedTensorType(value.type)
            if any(d < 0 for d in source_type.shape):
                raise ValueError("depthwise layout transformation requires static tensor shapes")
            result_type = ir.RankedTensorType.get([source_type.shape[i] for i in permutation],
                                                   source_type.element_type)
            empty = ir.Operation.create("tensor.empty", results=[result_type])
            op = ir.Operation.create("linalg.transpose", operands=[value, empty.results[0]],
                                     results=[result_type], regions=1,
                                     attributes={"permutation": ir.DenseI64ArrayAttr.get(permutation),
                                                 "operandSegmentSizes": ir.DenseI32ArrayAttr.get([1, 1])})
            body = op.regions[0].blocks.append(source_type.element_type, source_type.element_type)
            with ir.InsertionPoint(body):
                ir.Operation.create("linalg.yield", operands=[body.arguments[0]])
            return op.results[0]

        for original in candidates:
            if len(original.operands) != 3 or len(original.results) != 1 or len(original.regions) != 1:
                raise ValueError("unsupported depthwise operation contract")
            if any(ir.RankedTensorType(v.type).encoding is not None for v in original.operands):
                raise ValueError("encoded tensors require a dedicated layout conversion")
            # Move the scalar region intact; its computation does not depend
            # on the enclosing tensor dimension order.
            with ir.InsertionPoint(original):
                image = transpose(original.operands[0], [0, 2, 3, 1])
                weight = transpose(original.operands[1], [1, 2, 0])
                initial = transpose(original.operands[2], [0, 2, 3, 1])
                attrs = {key: value for key, value in original.attributes.items()
                         if key != "linalg.memoized_indexing_maps"}
                replacement = ir.Operation.create("linalg.depthwise_conv_2d_nhwc_hwc",
                                                  operands=[image, weight, initial],
                                                  results=[initial.type], attributes=attrs, regions=1)
                for block in list(original.regions[0].blocks):
                    block.append_to(replacement.regions[0])
                result = transpose(replacement.results[0], [0, 3, 1, 2])
                if result.type != original.results[0].type:
                    raise ValueError("layout transformation changed the external tensor type")
                original.results[0].replace_all_uses_with(result)
            original.erase()
        module.operation.verify()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(str(module) + "\n", "utf-8")
    return len(candidates)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    count = transform(args.input, args.output)
    print(json.dumps({"depthwiseConverted": count, "numericsValidated": False, "deviceValidated": False}))


if __name__ == "__main__":
    main()
