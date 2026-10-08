#!/usr/bin/env python3
"""Build the small C ABI runtime and the build-only parameter assembler."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import subprocess
from build_iree import head
from export_iree_hotspot import LOCK, digest


def build(source: Path, output: Path, jobs=2) -> dict:
    source,output=source.resolve(),output.resolve()
    lock=json.loads(LOCK.read_text())
    if head(source,lock.get("localSourceDiffSha256"))!=lock["amdAie"]["commit"]:
        raise ValueError("AMD AIE source commit mismatch")
    revisions={"amdAie":lock["amdAie"]["commit"]}
    for relative,revision in lock["amdAie"]["submodules"].items():
        if head(source/relative,lock.get("upstreamConfigureDiffSha256",{}).get(relative))!=revision:
            raise ValueError("AMD AIE submodule source mismatch")
        revisions[relative]=revision
    for relative,revision in lock["ireeSubmodules"].items():
        if head(source/"third_party/iree"/relative)!=revision:
            raise ValueError("IREE submodule source mismatch")
        revisions["iree/"+relative]=revision
    if jobs<1:raise ValueError("jobs must be positive")
    root=Path(__file__).parent.resolve()
    sources=[root/"runtime"/name for name in ("CMakeLists.txt","runtime.cc","merge_parameters.cc","exports.map")]
    sources.append(root.parents[1]/"src/inference/amdnpu/runtime_api.h")
    records=[{"path":p.relative_to(root.parents[1]).as_posix(),"sha256":digest(p)} for p in sources]
    configure=["cmake","-S",str(root/"runtime"),"-B",str(output),"-GNinja",
               "-DCMAKE_BUILD_TYPE=Release","-DCMAKE_C_COMPILER=clang","-DCMAKE_CXX_COMPILER=clang++",
               f"-DLIGHT_OCR_IREE_SOURCE={source/'third_party/iree'}",
               f"-DLIGHT_OCR_AMD_AIE_SOURCE={source}","-DIREE_INPUT_STABLEHLO=OFF",
               "-DIREE_INPUT_TORCH=OFF","-DIREE_INPUT_TOSA=OFF"]
    output.mkdir(parents=True,exist_ok=True)
    manifest_path=output/"runtime-build.json"
    manifest={"schemaVersion":"1.0","status":"building","sourceCommits":revisions,
              "sourceLockSha256":digest(LOCK),"wrapperSources":records,"configureCommand":configure,
              "deviceValidated":False,"numericsValidated":False}
    manifest_path.write_text(json.dumps(manifest,indent=2)+"\n")
    try:
        subprocess.run(configure,check=True)
        subprocess.run(["cmake","--build",str(output),"--target","light_ocr_amdaie",
                        "merge-aie-parameters","--parallel",str(jobs)],check=True)
        library=output/"liblight_ocr_amdaie.so.1.0.0"
        subprocess.run(["strip","--strip-unneeded",str(library)],check=True)
        manifest["binaries"]=[{"path":path.name,"bytes":path.stat().st_size,"sha256":digest(path)}
                              for path in (library,output/"merge-aie-parameters")]
        manifest["status"]="built"
    except BaseException:
        manifest["status"]="failed";raise
    finally:manifest_path.write_text(json.dumps(manifest,indent=2)+"\n")
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir",type=Path,required=True)
    parser.add_argument("--output-dir",type=Path,required=True)
    parser.add_argument("--jobs",type=int,default=2)
    args=parser.parse_args()
    result=build(args.source_dir,args.output_dir,args.jobs)
    print(f"AMD C ABI runtime: {result['binaries'][0]['bytes']} bytes")


if __name__=="__main__":main()
