#!/usr/bin/env python3
"""Turn an acquired Ryzen AI native deployment directory into a locked SDK.

The proprietary SDK is acquired separately from AMD. This tool neither
installs it nor changes system drivers, environment variables, or libraries.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

try:
    from .sdk import artifact_set, digest, record, recognition_widths, validate_sdk, verify_record
except ImportError:
    from sdk import artifact_set, digest, record, recognition_widths, validate_sdk, verify_record


def import_sdk(runtime: Path, models: Path, licenses: list[Path], output: Path, version: str,
               source_manifest: Path | None = None) -> dict:
    compiled = json.loads((models / "compile-manifest.json").read_text("utf-8"))
    if (compiled.get("schemaVersion") != "1.0" or compiled.get("precision") != "bf16" or
            [item["width"] for item in compiled["recognitionModels"]] != recognition_widths() or
            any(item.get("contextCount", 0) < 1 for item in compiled["recognitionModels"])):
        raise ValueError("AMD NPU requires all 20 precompiled BF16 context models")
    if not licenses or any(not item.is_file() for item in licenses):
        raise ValueError("supply the SDK and third-party license files for redistribution")
    if output.exists():
        raise ValueError("SDK output already exists; select a fresh output directory")
    patchelf = shutil.which("patchelf")
    if not patchelf:
        raise ValueError("patchelf is required to make the AMD library closure package-local")
    output.mkdir(parents=True)
    lib = output / "lib"
    lib.mkdir()
    provenance = []
    for source in sorted(runtime.iterdir()):
        if not re.fullmatch(r"lib[^/]+\.so(?:\.[A-Za-z0-9_-]+)*", source.name):
            continue
        if not source.resolve().is_file():
            raise ValueError(f"AMD runtime entry is not a file: {source}")
        destination = lib / source.name
        shutil.copyfile(source.resolve(), destination)
        source_hash = digest(destination)
        # AMD's Linux wheels contain ELF files marked with an executable
        # stack. Recent glibc rejects those files in dlopen/dlmopen. ELF
        # metadata is adjusted alongside the package-local search path, with
        # both the original and packaged hashes in provenance.
        subprocess.run([patchelf, "--clear-execstack", str(destination)], check=True, capture_output=True)
        subprocess.run([patchelf, "--set-rpath", "$ORIGIN:/opt/xilinx/xrt/lib", str(destination)],
                       check=True, capture_output=True)
        provenance.append({"path": destination.relative_to(output).as_posix(), "sourceSha256": source_hash,
                           "packagedSha256": digest(destination)})
    names = {item.name for item in lib.iterdir()}
    if "libonnxruntime_providers_vitisai.so" not in names:
        raise ValueError("AMD deployment directory has no VitisAI provider library")
    library = lib / "libonnxruntime.so"
    if not library.exists():
        candidates = sorted(lib.glob("libonnxruntime.so.*"))
        if len(candidates) != 1:
            raise ValueError("AMD deployment directory has no unambiguous ORT runtime")
        library = candidates[0]
    # Check the direct ELF closure, allowing only the OS and installed NPU
    # driver stack outside the bundle. No LD_LIBRARY_PATH is needed for SDK libs.
    system = {"libc.so.6", "libstdc++.so.6", "libgcc_s.so.1", "libm.so.6", "libdl.so.2",
              "libpthread.so.0", "librt.so.1", "ld-linux-x86-64.so.2"}
    for file in lib.iterdir():
        dynamic = subprocess.run(["readelf", "-d", str(file)], check=True, capture_output=True, text=True).stdout
        needed = set(re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic))
        missing = {name for name in needed - names - system if not name.startswith(("libxrt", "libdrm", "libudev"))}
        if missing:
            raise ValueError(f"incomplete AMD dependency closure for {file.name}: {sorted(missing)}")
    model_dir = output / "models"
    model_dir.mkdir()
    model_records = []
    for item in compiled["recognitionModels"]:
        source = verify_record(item["artifact"], models)
        destination = model_dir / f"rec-{item['width']}.onnx"
        shutil.copyfile(source, destination)
        model_records.append({"width": item["width"], "artifact": record(destination, output)})
    config_source = verify_record(compiled["compilerConfiguration"], models)
    config = output / "vaip_config.json"
    shutil.copyfile(config_source, config)
    license_dir = output / "licenses"
    license_dir.mkdir()
    for index, source in enumerate(licenses):
        shutil.copyfile(source, license_dir / f"{index}-{source.name}")
    artifacts = [record(path, output) for path in sorted(output.rglob("*"))
                 if path.is_file() and path.parent != license_dir]
    manifest = {"schemaVersion": "1.0", "provider": "amdnpu", "platformId": "linux-x64",
                "providerVersion": version, "qualificationId": f"amdnpu-{version}-linux-x64-opt-in-v1",
                "qualificationOnly": True, "runtimeLibrary": library.relative_to(output).as_posix(),
                "configuration": {"sourceModelSha256": compiled["sourceModelSha256"],
                                  "recognitionModels": model_records, "compilerConfiguration": record(config, output)},
                "artifacts": artifacts, "artifactSetSha256": artifact_set(artifacts),
                "files": [record(path, output) for path in sorted(output.rglob("*")) if path.is_file()],
                "licenses": [record(path, output) for path in sorted(license_dir.iterdir())],
                "provenance": provenance,
                "source": (json.loads(source_manifest.read_text("utf-8")) if source_manifest else
                           {"url": "https://ryzenai.docs.amd.com/en/latest/linux.html"})}
    (output / "sdk-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", "utf-8")
    return validate_sdk(output, "amdnpu")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime-dir", type=Path, required=True)
    parser.add_argument("--models-dir", type=Path, required=True)
    parser.add_argument("--license-file", type=Path, action="append", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--version", default="1.8.0")
    parser.add_argument("--source-manifest", type=Path,
                        help="Archive identities and compiler provenance for the acquired deployment payload")
    args = parser.parse_args()
    value = import_sdk(args.runtime_dir, args.models_dir, args.license_file, args.output_dir, args.version,
                       args.source_manifest)
    print(f"AMD NPU SDK: {value['artifactSetSha256']} (hardware inference not validated)")


if __name__ == "__main__":
    main()
