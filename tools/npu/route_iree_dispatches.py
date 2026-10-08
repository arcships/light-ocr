#!/usr/bin/env python3
"""Assign affinities after Flow formation and name shared weights by content.

Pre-Flow affinities can be lost during dispatch creation. Only the BF16
matmuls inserted by partition_iree_model.py are assigned to the AMD device.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re


def route(source: Path, output: Path, expected_npu_dispatches=19) -> dict:
    if output.exists():
        raise ValueError("output already exists")
    lines = source.read_text("utf-8").splitlines()
    out, renames = [], {}
    counts = {"cpu":0,"npu":0}
    kernel = False
    for line in lines:
        if line.startswith("module "):
            line = ('module attributes {stream.affinity.default = #hal.device.affinity<@npu>, '
                    'stream.topology = #hal.device.topology<links = '
                    '[(@cpu -> @npu = {transparent_access = true})]>} {')
            out.append(line)
            out.append('  util.global private @npu = #hal.device.target<"amdxdna", '
                       '[#hal.executable.target<"amd-aie", "amdaie-pdi-fb", '
                       '{num_cols = 2 : i32, num_rows = 2 : i32, target_device = "npu4", '
                       'ukernels = "matmul"}>]> : !hal.device')
            continue
        # The scalar barrier prevents CSE/constant hoisting from moving the
        # zero accumulator out of its dispatch. Dispatch formation is now
        # complete, so it is safe to remove this scalar-only barrier.
        if "func.func " in line:
            kernel = "_bf16xbf16xf32" in line
        if kernel and "util.optimization_barrier" in line:
            match = re.fullmatch(r'\s*(%[\w]+) = util.optimization_barrier (%[\w]+) : f32',line)
            if not match:
                raise ValueError("unexpected optimization barrier in NPU dispatch")
            barrier, replacement = match.groups()
            # Resolve only subsequent uses within this function.
            renames[barrier] = replacement
            continue
        if kernel:
            for old, new in renames.items():
                if old.startswith("%"):
                    line = re.sub(re.escape(old)+r'(?![\w])',new,line)
        if line.strip() == "return":
            kernel = False
            renames = {k:v for k,v in renames.items() if not k.startswith("%")}
        if " = flow.dispatch " in line:
            device = "npu" if "_bf16xbf16xf32" in line else "cpu"
            counts[device] += 1
            if "stream.affinity" in line or ") : " not in line:
                raise ValueError("unexpected dispatch syntax")
            line = line.replace(") : ",f") {{stream.affinity = #hal.device.affinity<@{device}>}} : ",1)
        match = re.match(r'\s*util.global private @([\w]+)(?: \{[^}]+\})? = (dense<.*)',line)
        if match:
            old, value = match.groups()
            renames[old] = "weight_"+hashlib.sha256(value.encode()).hexdigest()
            if re.search(r'tensor<[^>]*xbf16>',value):
                line = line.replace('#hal.device.affinity<@cpu>','#hal.device.affinity<@npu>')
        out.append(line)
    if counts["npu"] != expected_npu_dispatches or counts["cpu"] == 0:
        raise ValueError(f"source graph partition contract changed: {counts}")
    text = "\n".join(out)+"\n"
    text = re.sub(r'@([\w]+)',lambda m:"@"+renames.get(m[1],m[1]),text)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(text,"utf-8")
    return {"dispatches":counts,"sharedWeightKeys":len(renames),
            "cpuPartitionRequired":True,"deviceValidated":False,"numericsValidated":False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--report",type=Path,required=True)
    parser.add_argument("--expected-npu-dispatches",type=int,default=19)
    args=parser.parse_args()
    report=route(args.input,args.output,args.expected_npu_dispatches)
    args.report.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report))


if __name__=="__main__":
    main()
