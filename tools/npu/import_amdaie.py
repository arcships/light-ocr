#!/usr/bin/env python3
"""Assemble an opt-in AMD deployment SDK from compiled runtime/model artifacts."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
from export_iree_hotspot import LOCK, digest
from sdk import artifact_set, record, recognition_widths, validate_sdk, verify_record


def assemble(source, runtime, models, bundle, output):
    source,runtime,models,bundle,output=(p.resolve() for p in (source,runtime,models,bundle,output))
    lock=json.loads(LOCK.read_text())
    compiled=json.loads((models/"compile-report.json").read_text())
    built=json.loads((runtime/"runtime-build.json").read_text())
    if (compiled.get("status")!="compiled" or compiled.get("completeRecognitionContract") is not True or
        compiled.get("sourceModelSha256")!=lock["sourceModelSha256"] or
        compiled.get("sourceLockSha256")!=digest(LOCK) or
        [item["width"] for item in compiled["buckets"]]!=recognition_widths() or
        built.get("status")!="built" or built.get("sourceLockSha256")!=digest(LOCK) or
        built.get("sourceCommits")!=compiled.get("sourceCommits")):
        raise ValueError("deployment requires complete, identically pinned runtime and model builds")
    if output.exists():raise ValueError("select a fresh SDK output directory")
    library_record=next(item for item in built["binaries"] if item["path"]=="liblight_ocr_amdaie.so.1.0.0")
    library_source=verify_record(library_record,runtime)
    dynamic=subprocess.check_output(["readelf","-d",str(library_source)],text=True)
    needed=set(re.findall(r'\(NEEDED\).*\[([^\]]+)\]',dynamic))
    system={"libc.so.6","libstdc++.so.6","libgcc_s.so.1","libm.so.6","libdl.so.2",
            "libpthread.so.0","librt.so.1","ld-linux-x86-64.so.2","libuuid.so.1"}
    if needed-system:raise ValueError(f"runtime depends on non-system libraries: {needed-system}")
    output.mkdir(parents=True)
    library=output/"lib/liblight_ocr_amdaie.so.1";library.parent.mkdir()
    shutil.copyfile(library_source,library)
    model_dir=output/"models";model_dir.mkdir()
    records=[]
    for item in compiled["buckets"]:
        if (item["partition"]["npuPointwiseConvolutions"]!=19 or
            item["routing"]["dispatches"]["npu"]!=item["partition"]["npuDispatches"] or
            item["routing"]["dispatches"]["cpu"]<=0):
            raise ValueError("recognition model lacks the expected NPU/CPU partitions")
        original=verify_record(item["artifact"],models/"artifacts")
        destination=model_dir/original.name;shutil.copyfile(original,destination)
        records.append({"width":item["width"],"artifact":record(destination,output)})
    parameters=verify_record(compiled["parameters"],models/"artifacts")
    shared=model_dir/"recognition.irpa";shutil.copyfile(parameters,shared)
    version="iree-amd-aie-"+lock["amdAie"]["commit"][:12]
    configuration={"schemaVersion":"1.0","target":"IREEAMDAIE","runtimeAbi":1,
        "runtimeVersion":version,"sourceModelSha256":lock["sourceModelSha256"],
        "device":"npu4","parameterScope":"recognition","precision":"bf16-bfp16ebs8",
        "cpuPartitionRequired":True,"widths":recognition_widths(),"parameters":record(shared,output),
        "npuPointwiseConvolutions":19,"deviceValidated":False,"numericsValidated":False}
    config=output/"deployment.json";config.write_text(json.dumps(configuration,indent=2)+"\n")
    licenses=output/"licenses";licenses.mkdir()
    license_sources={"AMD-AIE-LICENSE":source/"LICENSE",
        "IREE-LICENSE":source/"third_party/iree/LICENSE",
        "LLVM-LICENSE":source/"third_party/iree/third_party/llvm-project/LICENSE.TXT",
        "flatcc-LICENSE":source/"third_party/iree/third_party/flatcc/LICENSE",
        "flatcc-NOTICE":source/"third_party/iree/third_party/flatcc/NOTICE"}
    for name,path in license_sources.items():shutil.copyfile(path,licenses/name)
    for path in sorted((bundle/"LICENSES").iterdir()):
        if path.is_file():shutil.copyfile(path,licenses/("model-"+path.name))
    artifacts=[record(path,output) for path in sorted(output.rglob("*"))
               if path.is_file() and path.parent!=licenses]
    value={"schemaVersion":"1.0","provider":"amdnpu","platformId":"linux-x64",
        "providerVersion":version,"qualificationId":f"amdnpu-aie-{lock['amdAie']['commit'][:12]}-opt-in-v1",
        "qualificationOnly":True,"runtimeLibrary":library.relative_to(output).as_posix(),
        "configuration":{"sourceModelSha256":lock["sourceModelSha256"],"recognitionModels":records,
                         "compilerConfiguration":record(config,output)},
        "artifacts":artifacts,"artifactSetSha256":artifact_set(artifacts),
        "files":[record(p,output) for p in sorted(output.rglob("*")) if p.is_file()],
        "licenses":[record(p,output) for p in sorted(licenses.iterdir())],
        "source":{"repository":lock["amdAie"]["repository"],"commit":lock["amdAie"]["commit"],
                  "runtimeBuildSha256":digest(runtime/"runtime-build.json"),
                  "modelBuildSha256":digest(models/"compile-report.json"),
                  "sourceLockSha256":digest(LOCK),"systemLibraries":sorted(needed)},
        "deviceValidated":False,"numericsValidated":False}
    (output/"sdk-manifest.json").write_text(json.dumps(value,indent=2)+"\n")
    validate_sdk(output,"amdnpu")
    return value


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("source-dir","runtime-build-dir","models-dir","model-bundle-dir","output-dir"):
        parser.add_argument("--"+name,type=Path,required=True)
    a=parser.parse_args()
    result=assemble(a.source_dir,a.runtime_build_dir,a.models_dir,a.model_bundle_dir,a.output_dir)
    print(f"AMD lightweight SDK: {sum(p['bytes'] for p in result['artifacts'])} bytes")


if __name__=="__main__":main()
