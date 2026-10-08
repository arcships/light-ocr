#!/usr/bin/env python3
"""Partition dense pointwise convolutions for the pinned AMD BF16 microkernel.

Run in the locked frontend environment. Layouts, padding, activation, other
convolutions and the classifier retain CPU FP32 execution. No inference runs.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def matrix_region(shapes):
    image, weight, result = shapes
    b, k, h, w = image
    n = weight[0]
    m = b * h * w
    pm, pk, pn = (((v + 127) // 128) * 128 for v in (m, k, n))
    def t(dims, dtype="f32"):
        return "tensor<" + "x".join(map(str, dims)) + "x" + dtype + ">"
    src, wt, dst = map(t, shapes)
    nhwc, whwc, flat, wf = t((b,h,w,k)), t((1,1,k,n)), t((m,k)), t((k,n))
    padded, pw = t((pm,pk)), t((pk,pn))
    a16, b16, c32, crop = t((pm,pk),"bf16"), t((pk,pn),"bf16"), t((pm,pn)), t((m,n))
    lines = ["#id = affine_map<(d0, d1) -> (d0, d1)>", "module {",
             f"func.func @region(%image: {src}, %weight: {wt}, %initial: {dst}) -> {dst} {{",
             "%zero = arith.constant 0.0 : f32"]
    def layout(name, value, source, target, perm):
        lines.extend([f"%{name}_empty = tensor.empty() : {target}",
                      f"%{name} = linalg.transpose ins(%{value} : {source}) outs(%{name}_empty : {target}) permutation = {perm}"])
    def pad(name, value, source, target, high):
        lines.extend([f"%{name} = tensor.pad %{value} low[0, 0] high{high} {{",
                      "^bb0(%i: index, %j: index):", "tensor.yield %zero : f32",
                      f"}} : {source} to {target}"])
    def cast(name, value, source, target):
        lines.extend([f"%{name}_empty = tensor.empty() : {target}",
                      f'%{name} = linalg.generic {{indexing_maps = [#id, #id], iterator_types = ["parallel", "parallel"]}} ins(%{value} : {source}) outs(%{name}_empty : {target}) {{',
                      "^bb0(%a: f32, %unused: bf16):", "%v = arith.truncf %a : f32 to bf16",
                      "linalg.yield %v : bf16", f"}} -> {target}"])
    layout("nhwc","image",src,nhwc,[0,2,3,1])
    layout("whwc","weight",wt,whwc,[2,3,1,0])
    lines.extend([f"%flat = tensor.collapse_shape %nhwc [[0, 1, 2], [3]] : {nhwc} into {flat}",
                  f"%wf = tensor.collapse_shape %whwc [[0, 1, 2], [3]] : {whwc} into {wf}"])
    pad("padded","flat",flat,padded,[pm-m,pk-k])
    pad("pw","wf",wf,pw,[pk-k,pn-n])
    cast("a16","padded",padded,a16)
    cast("b16","pw",pw,b16)
    lines.append(f"%b = util.optimization_barrier %b16 : {b16}")
    # The pinned shim DMA lowering retains too many address dimensions for
    # wide weight-repeat schedules. Use the largest row count compiled by
    # this model on 2x2 tiles, and split wider tensors at the CPU boundary.
    chunks = []
    for offset in range(0,pm,3840):
        rows=min(3840,pm-offset)
        index=len(chunks)
        left,result_type=t((rows,pk),"bf16"),t((rows,pn))
        lines.extend([f"%slice{index} = tensor.extract_slice %a16[{offset}, 0] [{rows}, {pk}] [1, 1] : {a16} to {left}",
                      f"%a{index} = util.optimization_barrier %slice{index} : {left}",
                      f"%mm{index} = flow.dispatch.region -> ({result_type}) {{",
                      f"%region_zero{index} = util.optimization_barrier %zero : f32",
                      f"%empty{index} = tensor.empty() : {result_type}",
                      f"%zeros{index} = linalg.fill ins(%region_zero{index} : f32) outs(%empty{index} : {result_type}) -> {result_type}",
                      f"%product{index} = linalg.matmul ins(%a{index}, %b : {left}, {b16}) outs(%zeros{index} : {result_type}) -> {result_type}",
                      f"flow.return %product{index} : {result_type}", "}",
                      f"%barrier{index} = util.optimization_barrier %mm{index} : {result_type}"])
        chunks.append((offset,rows,result_type))
    if len(chunks)==1:
        merged="%barrier0"
    else:
        lines.append(f"%assembled = tensor.empty() : {c32}")
        merged="%assembled"
        for index,(offset,rows,result_type) in enumerate(chunks):
            lines.append(f"%joined{index} = tensor.insert_slice %barrier{index} into {merged}[{offset}, 0] [{rows}, {pn}] [1, 1] : {result_type} into {c32}")
            merged=f"%joined{index}"
    lines.extend([
                  f"%crop = tensor.extract_slice {merged}[0, 0] [{m}, {n}] [1, 1] : {c32} to {crop}",
                  f"%expanded = tensor.expand_shape %crop [[0, 1, 2], [3]] output_shape [{b}, {h}, {w}, {n}] : {crop} into {t((b,h,w,n))}"])
    layout("output","expanded",t((b,h,w,n)),dst,[0,3,1,2])
    lines.extend([f"%sum_empty = tensor.empty() : {dst}",
                  f"%sum = linalg.add ins(%output, %initial : {dst}, {dst}) outs(%sum_empty : {dst}) -> {dst}",
                  f"return %sum : {dst}", "}", "}"])
    return "\n".join(lines), {"originalShapes": shapes,"paddedMatmul": [pm,pn,pk],
                              "npuDispatches":len(chunks),"maximumDispatchRows":3840}


def transform(source: Path, output: Path) -> dict:
    from iree.compiler import ir
    if output.exists():
        raise ValueError("output already exists")
    selected = []
    with ir.Context(), ir.Location.unknown():
        # bootgen treats whitespace in executable file names as separators.
        # Give the locked model a stable public entry point and safe symbols.
        module = ir.Module.parse(source.read_text("utf-8").replace(
            "PaddlePaddle Graph in PIR mode", "recognition"))
        candidates = []
        def walk(op):
            if op.name == "linalg.conv_2d_nchw_fchw":
                shapes = [list(ir.RankedTensorType(v.type).shape) for v in op.operands]
                if shapes[1][2:] == [1,1] and shapes[0][1] >= 128 and shapes[1][0] >= 128:
                    candidates.append((op,shapes))
            for region in op.regions:
                for block in region.blocks:
                    for child in block.operations:
                        walk(child.operation)
        walk(module.operation)
        for original, shapes in candidates:
            if any(d <= 0 for shape in shapes for d in shape):
                raise ValueError("static positive dimensions are required")
            if shapes[0][0] != shapes[2][0] or shapes[0][2:] != shapes[2][2:]:
                raise ValueError("pointwise spatial dimensions differ")
            for key in ("strides","dilations"):
                if any(int(v) != 1 for v in ir.DenseIntElementsAttr(original.attributes[key])):
                    raise ValueError("only unit-stride pointwise convolutions are supported")
            if any(str(ir.RankedTensorType(v.type).element_type) != "f32" for v in original.operands):
                raise ValueError("source must retain FP32 tensors")
            fragment, record = matrix_region(shapes)
            helper = ir.Module.parse(fragment)
            block = helper.body.operations[0].regions[0].blocks[0]
            for argument, operand in zip(block.arguments, original.operands):
                argument.replace_all_uses_with(operand)
            operations = list(block.operations)
            value = operations[-1].operands[0]
            for operation in operations[:-1]:
                operation.operation.move_before(original)
            original.results[0].replace_all_uses_with(value)
            original.erase()
            selected.append(record)
        if not selected:
            raise ValueError("no NPU partitions found")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(str(module)+"\n", "utf-8")
    return {"precision":"bf16-bfp16ebs8", "npuPointwiseConvolutions":len(selected),
            "npuDispatches":sum(p["npuDispatches"] for p in selected),
            "partitions":selected, "cpuPartitionRequired":True,
            "numericsValidated":False, "deviceValidated":False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--report",type=Path,required=True)
    args=parser.parse_args()
    report=transform(args.input,args.output)
    args.report.write_text(json.dumps(report,indent=2)+"\n")
    print(f"Partitioned {report['npuPointwiseConvolutions']} dense pointwise convolutions")


if __name__=="__main__":
    main()
