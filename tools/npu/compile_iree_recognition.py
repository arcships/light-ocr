#!/usr/bin/env python3
"""Build all Small recognition width buckets with explicit CPU/AMD partitions.

Only deployable VMFBs and one shared IRPA are written under artifacts/. Host
tools and compiler intermediates remain in work/. No inference or tests run.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys

from export_iree_hotspot import LOCK, WIDTHS, digest


def compile_models(source, frontend, toolchain, peano, merger, output,
                   widths=WIDTHS, timeout=900, keep_intermediates=False):
    source,frontend,toolchain,peano,merger,output=(p.resolve() for p in
        (source,frontend,toolchain,peano,merger,output))
    lock=json.loads(LOCK.read_text())
    if digest(source)!=lock["sourceModelSha256"]:
        raise ValueError("source recognition model differs from the lock")
    build=json.loads((toolchain/"toolchain-build.json").read_text())
    if (build.get("status")!="built" or build.get("kind")!="compiler" or
        build.get("localPatches")!=lock.get("localPatches") or
        build.get("localSourceDiffSha256")!=lock.get("localSourceDiffSha256") or
        build["sourceCommits"]["amdAie"]!=lock["amdAie"]["commit"] or
        build["sourceCommits"]["third_party/iree"]!=lock["amdAie"]["submodules"]["third_party/iree"]):
        raise ValueError("compiler source identity mismatch")
    for name in ("tools/iree-compile","tools/iree-opt","lib/libIREECompiler.so"):
        record=next(item for item in build["binaries"] if item["path"]==name)
        if (toolchain/name).stat().st_size!=record["bytes"] or digest(toolchain/name)!=record["sha256"]:
            raise ValueError("compiler binary identity mismatch")
    provenance=json.loads((peano/"toolchain-source.json").read_text())
    if provenance["source"]!=lock["peano"]:
        raise ValueError("Peano source identity mismatch")
    for record in provenance["binaries"]:
        if digest(peano/record["path"])!=record["sha256"]:
            raise ValueError("Peano executable identity mismatch")
    if output.exists():
        raise ValueError("select a fresh output directory")
    if not widths or any(w not in WIDTHS for w in widths) or len(set(widths))!=len(widths):
        raise ValueError("invalid recognition widths")
    output.mkdir(parents=True)
    artifacts=output/"artifacts"; artifacts.mkdir()
    tools=Path(__file__).parent.resolve()
    python=frontend/"bin/python"
    compiler=toolchain/"tools/iree-compile"
    optimizer=toolchain/"tools/iree-opt"
    report={"schemaVersion":"1.0","status":"building","sourceModelSha256":digest(source),
            "sourceLockSha256":digest(LOCK),"sourceCommits":build["sourceCommits"],
            "compilerBinaries":build["binaries"],"localPatches":build["localPatches"],
            "partitionScriptSha256":digest(tools/"partition_iree_model.py"),
            "routeScriptSha256":digest(tools/"route_iree_dispatches.py"),
            "parameterMergerSha256":digest(merger),"widths":list(widths),
            "precision":"bf16-bfp16ebs8","cpuPartitionRequired":True,
            "numericsValidated":False,"deviceValidated":False,"buckets":[]}
    report_path=output/"compile-report.json"
    def save():report_path.write_text(json.dumps(report,indent=2)+"\n")
    def run(command, log):
        with log.open("wb") as stream:
            process=subprocess.Popen(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT,
                                     start_new_session=True)
            try:
                code=process.wait(timeout=timeout)
                if code:raise RuntimeError(f"build command failed ({code}); see {log}")
            except BaseException:
                if process.poll() is None:
                    os.killpg(process.pid,signal.SIGKILL);process.wait()
                raise
    def record(path):return {"path":path.relative_to(artifacts).as_posix(),
                             "bytes":path.stat().st_size,"sha256":digest(path)}
    save()
    archives=[]
    try:
        for width in widths:
            work=output/"work"/str(width);work.mkdir(parents=True)
            model, imported=work/"model",work/"imported"
            entry={"width":width,"status":"building"};report["buckets"].append(entry);save()
            print(f"Building recognition width={width}",flush=True)
            run([python,tools/"export_iree_model.py","--source-model",source,
                 "--output-dir",model,"--width",width],work/"export.log")
            run([sys.executable,tools/"import_iree_model.py","--model-dir",model,
                 "--frontend-environment",frontend,"--output-dir",imported,
                 "--timeout",timeout],work/"import.log")
            partitioned,flow,routed,parameters=(work/name for name in
                ("partitioned.mlir","flow.mlir","routed.mlir","parameters.mlir"))
            run([python,tools/"partition_iree_model.py","--input",imported/"core-linalg.mlir",
                 "--output",partitioned,"--report",work/"partitions.json"],work/"partition.log")
            partition=json.loads((work/"partitions.json").read_text())
            run([compiler,partitioned,"--iree-hal-target-device=cpu=local",
                 "--iree-hal-local-target-device-backends=llvm-cpu","--iree-llvmcpu-target-cpu=generic",
                 "--compile-to=flow","-o",flow],work/"flow.log")
            run([sys.executable,tools/"route_iree_dispatches.py","--input",flow,"--output",routed,
                 "--report",work/"routing.json","--expected-npu-dispatches",
                 partition["npuDispatches"]],work/"route.log")
            archive=work/"parameters.irpa";archives.append(archive)
            run([optimizer,routed,f"--iree-io-export-parameters=path=recognition={archive} minimum-size=256",
                 "--canonicalize","--cse","-o",parameters],work/"parameters.log")
            artifact=artifacts/f"recognition-{width}.vmfb"
            run([compiler,parameters,"--compile-from=flow",
                 "--iree-hal-local-target-device-backends=llvm-cpu","--iree-llvmcpu-target-cpu=generic",
                 "--iree-amdaie-target-device=npu4","--iree-amdaie-device-hal=amdxdna",
                 "--iree-amdaie-num-cols=2","--iree-amdaie-num-rows=2",
                 "--iree-amdaie-enable-ukernels=matmul","--iree-amd-aie-enable-chess-for-ukernel=false",
                 f"--iree-amd-aie-peano-install-dir={peano}","--iree-hal-memoization=false",
                 "--iree-hal-indirect-command-buffers=false","--iree-amdaie-lower-to-aie-pipeline=objectFifo",
                 "--iree-amdaie-tile-pipeline=pack-peel","-o",artifact],work/"binary.log")
            entry.update(status="compiled",artifact=record(artifact),partition=partition,
                         routing=json.loads((work/"routing.json").read_text()))
            save()
            if not keep_intermediates:
                shutil.rmtree(model);shutil.rmtree(imported)
                for path in (partitioned,flow,routed,parameters):path.unlink()
        shared=artifacts/"recognition.irpa"
        run([merger,shared,*archives],output/"merge-parameters.log")
        report.update(status="compiled",parameters=record(shared),
                      completeRecognitionContract=list(widths)==list(WIDTHS),
                      artifactBytes=sum(e["artifact"]["bytes"] for e in report["buckets"])+shared.stat().st_size)
        save()
        if not keep_intermediates:
            for path in archives:path.unlink()
    except BaseException:
        report["status"]="failed";save();raise
    print(f"Compiled {len(widths)} width buckets; deployment models {report['artifactBytes']} bytes",flush=True)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("source-model","frontend-environment","toolchain-dir","peano-dir","parameter-merger","output-dir"):
        parser.add_argument("--"+name,type=Path,required=True)
    parser.add_argument("--width",type=int,choices=WIDTHS,action="append")
    parser.add_argument("--timeout",type=int,default=900)
    parser.add_argument("--keep-intermediates",action="store_true")
    a=parser.parse_args()
    if a.timeout<=0:parser.error("timeout must be positive")
    compile_models(a.source_model,a.frontend_environment,a.toolchain_dir,a.peano_dir,
                   a.parameter_merger,a.output_dir,a.width or WIDTHS,a.timeout,a.keep_intermediates)


if __name__=="__main__":main()
