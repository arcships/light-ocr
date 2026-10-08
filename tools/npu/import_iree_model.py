#!/usr/bin/env python3
"""Import a specialized OCR graph into core Linalg and channels-last convs.

The build-only frontend is separate from the pinned AMD compiler. Its output
must still be accepted by that compiler before claiming AMD support.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

try:
    from .export_iree_hotspot import LOCK, digest
except ImportError:
    from export_iree_hotspot import LOCK, digest


def import_model(model: Path, environment: Path, output: Path, timeout=600) -> dict:
    if timeout <= 0:
        raise ValueError("timeout must be positive")
    model, environment, output = (p.resolve() for p in (model, environment, output))
    if output.exists():
        raise ValueError("output exists; select a fresh directory")
    lock = json.loads(LOCK.read_text("utf-8"))
    manifest = json.loads((model / "model-manifest.json").read_text("utf-8"))
    if manifest["sourceModelSha256"] != lock["sourceModelSha256"]:
        raise ValueError("model source identity mismatch")
    source = model / "recognition-opset17.onnx"
    record = next(x for x in manifest["files"] if x["path"] == source.name)
    if source.stat().st_size != record["bytes"] or digest(source) != record["sha256"]:
        raise ValueError("specialized model identity mismatch")
    python, optimizer = environment / "bin/python", environment / "bin/iree-opt"
    requirements = Path(__file__).with_name("iree-frontend.requirements.txt")
    required = dict(line.strip().split("==", 1) for line in requirements.read_text("utf-8").splitlines()
                    if line.strip() and not line.lstrip().startswith("#"))
    versions = json.loads(subprocess.check_output(
        [str(python), "-c", "import importlib.metadata as m,json,sys; print(json.dumps({x:m.version(x) for x in json.loads(sys.argv[1])}))",
         json.dumps(list(required))], text=True))
    if versions != required:
        raise ValueError("frontend versions differ from iree-frontend.requirements.txt")
    compiler_library = Path(subprocess.check_output(
        [str(python), "-c", "import pathlib,iree.compiler; print(pathlib.Path(iree.compiler.__file__).parent/'_mlir_libs'/'libIREECompiler.so')"],
        text=True).strip())
    output.mkdir(parents=True)
    raw, core = output / "imported.mlir", output / "core-linalg.mlir"
    conv, depthwise, final = (output / name for name in
                              ("channels-last.mlir", "depthwise-channels-last.mlir", "model.mlir"))
    commands = [
        ("onnx-import", [str(python), "-m", "iree.compiler.tools.import_onnx", str(source), "-o", str(raw)]),
        ("core-linalg", [str(optimizer), str(raw), "--torch-onnx-to-torch-backend-pipeline",
                         "--torch-backend-to-linalg-on-tensors-backend-pipeline", "-o", str(core)]),
        ("conv-layout", [str(optimizer), str(core), "--iree-preprocessing-convert-conv-to-channels-last",
                         "--canonicalize", "--cse", "-o", str(conv)]),
        ("depthwise-layout", [str(python), str(Path(__file__).with_name("transform_iree_depthwise.py").resolve()),
                              "--input", str(conv), "--output", str(depthwise)]),
        ("canonicalize", [str(optimizer), str(depthwise), "--canonicalize", "--cse", "-o", str(final)]),
    ]
    report = {"schemaVersion": "1.0", "stage": "development", "status": "importing",
              "sourceModelSha256": manifest["sourceModelSha256"], "modelManifestSha256": digest(model / "model-manifest.json"),
              "width": manifest["width"], "frontendVersions": versions,
              "frontendRequirementsSha256": digest(requirements),
              "frontendCompilerLibrarySha256": digest(compiler_library),
              "frontendOptimizerSha256": digest(optimizer), "numericsValidated": False,
              "deviceValidated": False, "amdCompilerAccepted": False, "steps": []}
    path = output / "import-report.json"

    def save():
        path.write_text(json.dumps(report, indent=2) + "\n", "utf-8")

    save()
    for name, command in commands:
        step = {"name": name, "command": command, "status": "running"}
        report["steps"].append(step)
        save()
        log = output / f"{name}.log"
        with log.open("wb") as stream:
            process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=timeout)
                step.update(returnCode=code, status="completed" if code == 0 else "failed")
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                step["status"] = "timeout"
            except BaseException:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                step["status"] = "interrupted"
                report["status"] = "interrupted"
                save()
                raise
        step["log"] = {"path": log.name, "bytes": log.stat().st_size, "sha256": digest(log)}
        if step["status"] != "completed":
            report["status"] = "failed"
            save()
            return report
        save()
    report["status"] = "imported"
    report["artifact"] = {"path": final.name, "bytes": final.stat().st_size, "sha256": digest(final)}
    save()
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-dir", type=Path, required=True)
    parser.add_argument("--frontend-environment", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    result = import_model(args.model_dir, args.frontend_environment, args.output_dir, args.timeout)
    print(f"Recognition graph: {result['status']}; AMD compiler acceptance remains unverified")
    sys.exit(0 if result["status"] == "imported" else 1)


if __name__ == "__main__":
    main()
