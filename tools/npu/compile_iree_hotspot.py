#!/usr/bin/env python3
"""Compile source-bound OCR development IR and retain diagnostics on failure."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

LOCK = Path(__file__).with_name("iree-source.lock.json")


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def compile_hotspot(hotspot: Path, toolchain: Path, peano: Path, output: Path,
                    stage="targets", timeout=600, variant="all", imported_model=False,
                    kernel_mode="vector", core_rows=2, core_cols=2) -> dict:
    if timeout <= 0:
        raise ValueError("timeout must be positive")
    if not 1 <= core_rows <= 4 or not 1 <= core_cols <= 8:
        raise ValueError("npu4 core array must fit 4 rows and 8 columns")
    hotspot, toolchain, peano, output = (p.resolve() for p in (hotspot, toolchain, peano, output))
    if output.exists():
        raise ValueError("output exists; select a fresh directory")
    lock = json.loads(LOCK.read_text("utf-8"))
    manifest_path = hotspot / ("import-report.json" if imported_model else "hotspot-manifest.json")
    manifest = json.loads(manifest_path.read_text("utf-8"))
    if imported_model and (manifest.get("status") != "imported" or variant != "all"):
        raise ValueError("full graph requires a completed import and no hotspot variant selection")
    build = json.loads((toolchain / "toolchain-build.json").read_text("utf-8"))
    if (build.get("kind") != "compiler" or build["status"] != "built" or
            build["sourceCommits"]["amdAie"] != lock["amdAie"]["commit"] or
            build["sourceCommits"]["third_party/iree"] != lock["amdAie"]["submodules"]["third_party/iree"]):
        raise ValueError("compiler must come from the completed locked build")
    if (build.get("localPatches", []) != lock.get("localPatches", []) or
            build.get("localSourceDiffSha256") != lock.get("localSourceDiffSha256") or
            build.get("upstreamConfigureDiffSha256") != lock.get("upstreamConfigureDiffSha256")):
        raise ValueError("compiler source patch provenance mismatch")
    if manifest["sourceModelSha256"] != lock["sourceModelSha256"]:
        raise ValueError("hotspot source model identity mismatch")
    compiler = toolchain / "tools/iree-compile"
    if not compiler.is_file():
        raise ValueError("iree-compile is missing")
    binary = next(item for item in build["binaries"] if item["path"] == "tools/iree-compile")
    if compiler.stat().st_size != binary["bytes"] or digest(compiler) != binary["sha256"]:
        raise ValueError("compiler binary identity mismatch")
    library = toolchain / "lib/libIREECompiler.so"
    library_record = next(item for item in build["binaries"] if item["path"] == "lib/libIREECompiler.so")
    if library.stat().st_size != library_record["bytes"] or digest(library) != library_record["sha256"]:
        raise ValueError("compiler library identity mismatch")
    peano_source = json.loads((peano / "toolchain-source.json").read_text("utf-8"))
    if peano_source["source"] != lock["peano"]:
        raise ValueError("Peano source identity mismatch")
    for name in ("bin/clang", "bin/ld.lld"):
        item = next(item for item in peano_source["binaries"] if item["path"] == name)
        if digest(peano / name) != item["sha256"]:
            raise ValueError("Peano binary identity mismatch")
    records = ({manifest["artifact"]["path"]: manifest["artifact"]} if imported_model else
               {item["path"]: item for item in manifest["files"]})
    variants = (("recognition",) if imported_model else
                (("projection-matmul", "projection-matmul-padded", "projection", "mlp-residual") if variant == "all" else (variant,)))
    sources = {name: hotspot / ("model.mlir" if imported_model else f"{name}.mlir") for name in variants}
    for name in variants:
        source = sources[name]
        record = records[source.name]
        if source.stat().st_size != record["bytes"] or digest(source) != record["sha256"]:
            raise ValueError(f"IR identity mismatch: {name}")
    output.mkdir(parents=True)
    report = {"schemaVersion": "1.0", "stage": "development", "compileStage": stage,
              "width": manifest["width"], "sourceModelSha256": manifest["sourceModelSha256"],
              "inputKind": "recognition-graph" if imported_model else "hotspot",
              "inputManifestSha256": digest(manifest_path),
              "sourceLockSha256": digest(LOCK),
              "compilerSha256": digest(compiler), "sourceCommits": build["sourceCommits"],
              "compilerLibrarySha256": digest(library),
              "target": "npu4", "deviceHAL": "amdxdna", "deviceValidated": False,
              "coreRows": core_rows, "coreCols": core_cols, "kernelMode": kernel_mode,
              "kernelPrecision": ("bf16 converted to bfp16ebs8 inside Peano matmul microkernel"
                                  if kernel_mode == "peano-bf16" else "source IR types"),
              "numericsValidated": False, "deployableArtifactProduced": False, "results": []}
    report_path = output / "compile-report.json"

    def save():
        report_path.write_text(json.dumps(report, indent=2) + "\n", "utf-8")

    save()
    for name in variants:
        extension = "vmfb" if stage == "binary" else "mlir"
        artifact = output / f"{name}.{extension}"
        command = [str(compiler), str(sources[name]),
                   "--iree-hal-target-backends=amd-aie", "--iree-amdaie-target-device=npu4",
                   "--iree-amdaie-device-hal=amdxdna",
                   f"--iree-amdaie-num-rows={core_rows}", f"--iree-amdaie-num-cols={core_cols}",
                   "--iree-amdaie-lower-to-aie-pipeline=objectFifo",
                   "--iree-amdaie-tile-pipeline=pack-peel",
                   f"--iree-amd-aie-peano-install-dir={peano}",
                   "--iree-hal-memoization=false", "--iree-hal-indirect-command-buffers=false",
                   f"--iree-hal-dump-executable-files-to={output / (name + '-executables')}",
                   "-o", str(artifact)]
        if stage in ("sources", "targets"):
            command.append(f"--compile-to=executable-{stage}")
        if kernel_mode == "peano-bf16":
            command.extend(["--iree-amdaie-enable-ukernels=matmul", "--iree-amd-aie-enable-chess-for-ukernel=false"])
        entry = {"variant": name, "command": command, "status": "compiling"}
        report["results"].append(entry)
        save()
        log = output / f"{name}.log"
        with log.open("wb") as stream:
            process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=timeout)
                entry.update(returnCode=code, status="compiled" if code == 0 else "failed")
                if code < 0:
                    entry.update(status="crashed", signal=-code)
            except subprocess.TimeoutExpired:
                # Peano/bootgen children belong to this compilation, too.
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                entry["status"] = "timeout"
            except BaseException:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                entry["status"] = "interrupted"
                report["status"] = "interrupted"
                save()
                raise
        entry["log"] = {"path": log.name, "bytes": log.stat().st_size, "sha256": digest(log)}
        if entry["status"] == "compiled":
            if not artifact.is_file() or artifact.stat().st_size == 0:
                entry.update(status="failed", reason="compiler produced no artifact")
            else:
                entry["artifact"] = {"path": artifact.name, "bytes": artifact.stat().st_size,
                                     "sha256": digest(artifact)}
        save()
    report["status"] = "compiled" if all(x["status"] == "compiled" for x in report["results"]) else "failed"
    # Lowered textual IR is not a deployable model, even when lowering succeeds.
    report["deployableArtifactProduced"] = stage == "binary" and report["status"] == "compiled"
    save()
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--hotspot-dir", type=Path)
    inputs.add_argument("--model-dir", type=Path, help="directory produced by import_iree_model.py")
    parser.add_argument("--toolchain-build-dir", type=Path, required=True)
    parser.add_argument("--peano-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--stage", choices=("sources", "targets", "binary"), default="targets")
    parser.add_argument("--variant", choices=("projection-matmul", "projection-matmul-padded", "projection", "mlp-residual", "all"), default="all")
    parser.add_argument("--kernel-mode", choices=("vector", "peano-bf16"), default="vector")
    parser.add_argument("--core-rows", type=int, default=2)
    parser.add_argument("--core-cols", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    result = compile_hotspot(args.model_dir or args.hotspot_dir, args.toolchain_build_dir, args.peano_dir,
                             args.output_dir, args.stage, args.timeout, args.variant, args.model_dir is not None,
                             args.kernel_mode, args.core_rows, args.core_cols)
    print(f"AMD AIE {result['inputKind']}: {result['status']}; device/numerics remain unvalidated")
    sys.exit(0 if result["status"] == "compiled" else 1)


if __name__ == "__main__":
    main()
