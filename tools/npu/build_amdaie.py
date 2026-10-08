#!/usr/bin/env python3
"""Reproducibly build the optional AMD SDK without the proprietary Ryzen SDK."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import build_amdaie_runtime
import build_iree
import compile_iree_recognition
import fetch_iree_source
import fetch_peano
import import_amdaie
from export_iree_hotspot import LOCK, digest
from sdk import recognition_widths, verify_record


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir",type=Path,required=True)
    parser.add_argument("--output-dir",type=Path,required=True)
    parser.add_argument("--jobs",type=int,default=2)
    args=parser.parse_args()
    if sys.version_info[:2]!=(3,12):parser.error("build frontend requires Python 3.12")
    if args.jobs<1:parser.error("jobs must be positive")
    if args.output_dir.exists():parser.error("select a fresh SDK output directory")
    work=args.work_dir.resolve();work.mkdir(parents=True,exist_ok=True)
    tools=Path(__file__).parent.resolve();repo=tools.parents[1]
    source=fetch_iree_source.fetch(work/"source",jobs=args.jobs)
    peano=work/"peano/llvm-aie"
    if not peano.exists():peano=fetch_peano.fetch(work/"downloads",work/"peano")
    build_iree.build(source,peano,work/"toolchain-build",args.jobs)
    frontend=work/"frontend"
    if not frontend.exists():subprocess.run([sys.executable,"-m","venv",str(frontend)],check=True)
    subprocess.run([str(frontend/"bin/python"),"-m","pip","install","-r",
                    str(tools/"iree-frontend.requirements.txt")],check=True)
    bundle=repo/"models/generated/ppocrv6-small-onnx-20260714.2"
    if not bundle.exists():
        subprocess.run([sys.executable,str(repo/"tools/bootstrap_models.py"),"--tier","small",
                        "--output",str(bundle),"--cache-dir",str(work/"model-downloads")],check=True)
    runtime=work/"deployment-runtime-build"
    build_amdaie_runtime.build(source,runtime,args.jobs)
    inputs=[LOCK,tools/"iree-frontend.requirements.txt"]+[tools/name for name in
        ("compile_iree_recognition.py","partition_iree_model.py","route_iree_dispatches.py",
         "export_iree_model.py","import_iree_model.py","runtime/merge_parameters.cc")]
    key=hashlib.sha256("".join(digest(p) for p in inputs).encode()).hexdigest()[:16]
    models=work/("recognition-"+key)
    report_path=models/"compile-report.json"
    if report_path.exists():
        report=json.loads(report_path.read_text())
        if (report.get("status")!="compiled" or report.get("widths")!=recognition_widths() or
            report.get("sourceLockSha256")!=digest(LOCK) or
            report.get("parameterMergerSha256")!=digest(runtime/"merge-aie-parameters")):
            raise ValueError(f"incomplete or stale model cache: {models}; move it aside before rebuilding")
        for item in report["buckets"]:verify_record(item["artifact"],models/"artifacts")
        verify_record(report["parameters"],models/"artifacts")
    else:
        compile_iree_recognition.compile_models(bundle/"rec/inference.onnx",frontend,
            work/"toolchain-build",peano,runtime/"merge-aie-parameters",models)
    result=import_amdaie.assemble(source,runtime,models,bundle,args.output_dir)
    print(f"Optional AMD SDK: {sum(r['bytes'] for r in result['artifacts'])} bytes; no inference executed")


if __name__=="__main__":main()
